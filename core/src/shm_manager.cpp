#include "shm_manager.h"
#include <utils/flog.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fftw3.h>
#include <volk/volk.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace shm_manager {
#ifdef _WIN32
    static HANDLE hShm = NULL;
    static ShmHeader* shmHeader = NULL;

    static HANDLE hCmdShm = NULL;
    static ShmCmdBuffer* shmCmd = NULL;
#endif

    static std::mutex shmMtx;
    static CommandHandler customCmdHandler = nullptr;

    // === FFT Engine State ===
    static const int FFT_SIZE = 1024;
    static fftwf_complex* fftwIn = nullptr;
    static fftwf_complex* fftwOut = nullptr;
    static fftwf_plan fftPlan = nullptr;
    static float windowLut[FFT_SIZE];
    static float fftOutputDb[FFT_SIZE];
    static std::mutex fftMutex;
    static std::chrono::steady_clock::time_point lastFftTime;

    static void initFft() {
        if (fftwIn) return;
        fftwIn = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        fftwOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        fftPlan = fftwf_plan_dft_1d(FFT_SIZE, fftwIn, fftwOut, FFTW_FORWARD, FFTW_ESTIMATE);

        // Precompute Blackman-Harris 4-term window
        for (int i = 0; i < FFT_SIZE; i++) {
            double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
            double f = (2.0 * M_PI * i) / (FFT_SIZE - 1);
            windowLut[i] = (float)(a0 - a1 * cos(f) + a2 * cos(2.0 * f) - a3 * cos(3.0 * f));
        }
        lastFftTime = std::chrono::steady_clock::now();
    }

    static void cleanupFft() {
        if (fftPlan) { fftwf_destroy_plan(fftPlan); fftPlan = nullptr; }
        if (fftwIn) { fftwf_free(fftwIn); fftwIn = nullptr; }
        if (fftwOut) { fftwf_free(fftwOut); fftwOut = nullptr; }
    }

    bool init() {
        initFft();
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(shmMtx);
        if (shmHeader) return true;

        hShm = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(ShmHeader),
            SDRPP_SHM_NAME
        );

        if (!hShm) {
            flog::error("Failed to create Shared Memory mapping: {0}", (int)GetLastError());
            return false;
        }

        shmHeader = (ShmHeader*)MapViewOfFile(hShm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmHeader));
        if (!shmHeader) {
            flog::error("Failed to map Shared Memory view: {0}", (int)GetLastError());
            CloseHandle(hShm);
            hShm = NULL;
            return false;
        }

        std::memset(shmHeader, 0, sizeof(ShmHeader));
        shmHeader->magic = SDRPP_SHM_MAGIC;
        shmHeader->version = 1;
        shmHeader->fftSize = 1024;
        shmHeader->sampleRate = 8000000.0;
        shmHeader->centerFreq = 2400000000.0;
        shmHeader->lnaGain = 32;
        shmHeader->vgaGain = 20;

        // Command Buffer Shared Memory
        hCmdShm = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            sizeof(ShmCmdBuffer),
            SDRPP_SHM_CMD_NAME
        );

        if (hCmdShm) {
            shmCmd = (ShmCmdBuffer*)MapViewOfFile(hCmdShm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmCmdBuffer));
            if (shmCmd) {
                std::memset(shmCmd, 0, sizeof(ShmCmdBuffer));
            }
        }

        flog::info("⚡ Windows Native Shared Memory (Zero-Copy IPC) Engine Initialized (Address: 0x{0:X})", (uintptr_t)shmHeader);
        return true;
#else
        return false;
#endif
    }

    void cleanup() {
        cleanupFft();
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(shmMtx);
        if (shmCmd) {
            UnmapViewOfFile(shmCmd);
            shmCmd = NULL;
        }
        if (hCmdShm) {
            CloseHandle(hCmdShm);
            hCmdShm = NULL;
        }
        if (shmHeader) {
            UnmapViewOfFile(shmHeader);
            shmHeader = NULL;
        }
        if (hShm) {
            CloseHandle(hShm);
            hShm = NULL;
        }
#endif
    }

    void processIqSamples(const dsp::complex_t* samples, int count, double sampleRate) {
        if (!samples || count < FFT_SIZE || !fftPlan) return;

        // Rate limit FFT to ~60 FPS (16ms)
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFftTime).count();
        if (elapsedMs < 16) return;
        lastFftTime = now;

        std::unique_lock<std::mutex> lock(fftMutex, std::try_to_lock);
        if (!lock.owns_lock()) return;

        // Apply window and copy into FFTW input
        for (int i = 0; i < FFT_SIZE; i++) {
            fftwIn[i][0] = samples[i].re * windowLut[i];
            fftwIn[i][1] = samples[i].im * windowLut[i];
        }

        fftwf_execute(fftPlan);

        // Compute power in dBm with proper 1/N normalization
        int halfFft = FFT_SIZE / 2;
        float norm = 1.0f / (float)FFT_SIZE;
        for (int i = 0; i < FFT_SIZE; i++) {
            int srcIdx = (i + halfFft) % FFT_SIZE;
            float re = fftwOut[srcIdx][0] * norm;
            float im = fftwOut[srcIdx][1] * norm;
            float pwr = re * re + im * im + 1e-12f;
            float dbm = 10.0f * log10f(pwr);
            fftOutputDb[i] = std::max(-120.0f, std::min(10.0f, dbm));
        }

        // Direct Ultra-Fast write into Windows Shared Memory (< 0.001 ms)
        updateFft(fftOutputDb, FFT_SIZE);
    }

    void updateFft(const float* fftDb, int size) {
#ifdef _WIN32
        if (!shmHeader || !fftDb || size <= 0) return;
        int copySize = std::min(size, 1024);
        std::memcpy(shmHeader->fftData, fftDb, copySize * sizeof(float));
        MemoryBarrier();
        shmHeader->seq++;
#endif
    }

    void pushPacket(const nlohmann::json& packet) {
#ifdef _WIN32
        if (!shmHeader) return;
        std::lock_guard<std::mutex> lock(shmMtx);

        uint32_t idx = shmHeader->packetWriteIdx % 32;
        ShmPacket& p = shmHeader->packets[idx];
        std::memset(&p, 0, sizeof(ShmPacket));

        p.id = packet.value("id", 0);
        if (packet.contains("syncWord") && packet["syncWord"].is_string()) {
            std::string sw = packet["syncWord"];
            p.syncWord = (uint32_t)std::stoul(sw, nullptr, 0);
        }
        if (packet.contains("mask") && packet["mask"].is_string()) {
            std::string m = packet["mask"];
            p.mask = (uint8_t)std::stoul(m, nullptr, 0);
        }
        p.crcValid = packet.value("crcValid", false) ? 1 : 0;
        p.freqOffsetKhz = (float)packet.value("freqOffsetKhz", 0.0);

        std::string ts = packet.value("timestamp", "");
        strncpy(p.timestamp, ts.c_str(), sizeof(p.timestamp) - 1);

        if (packet.contains("payloadHex") && packet["payloadHex"].is_string()) {
            std::string hex = packet["payloadHex"];
            size_t pLen = 0;
            for (size_t i = 0; i + 1 < hex.size() && pLen < sizeof(p.payload); i++) {
                if (hex[i] == ' ') continue;
                if (isxdigit(hex[i]) && isxdigit(hex[i + 1])) {
                    char hexByte[3] = { hex[i], hex[i + 1], 0 };
                    p.payload[pLen++] = (uint8_t)strtoul(hexByte, nullptr, 16);
                    i++;
                }
            }
            p.payloadLen = (uint16_t)pLen;
        }

        shmHeader->packetWriteIdx++;
        MemoryBarrier();
        shmHeader->packetSeq++;
#endif
    }

    void updateState(bool running, const std::string& sourceName, double centerFreq, double sampleRate, int lna, int vga, bool amp, bool biasT, const std::string& serial) {
#ifdef _WIN32
        if (!shmHeader) return;
        shmHeader->running = running ? 1 : 0;
        shmHeader->centerFreq = centerFreq;
        shmHeader->sampleRate = sampleRate;
        shmHeader->lnaGain = lna;
        shmHeader->vgaGain = vga;
        shmHeader->ampEnable = amp ? 1 : 0;
        shmHeader->biasTEnable = biasT ? 1 : 0;
        strncpy(shmHeader->sourceName, sourceName.c_str(), sizeof(shmHeader->sourceName) - 1);
        strncpy(shmHeader->deviceSerial, serial.c_str(), sizeof(shmHeader->deviceSerial) - 1);
#endif
    }

    void setCommandHandler(CommandHandler handler) {
        customCmdHandler = handler;
    }

    void checkCommands() {
#ifdef _WIN32
        if (!shmCmd || !customCmdHandler) return;
        if (shmCmd->cmdSeq != shmCmd->ackSeq) {
            std::string cmd = shmCmd->cmd;
            nlohmann::json params;
            if (cmd == "set_freq") {
                params["freq"] = shmCmd->paramDouble;
            } else if (cmd == "set_samplerate") {
                params["sampleRate"] = shmCmd->paramDouble;
            } else if (cmd == "set_gain") {
                params["lna"] = shmCmd->paramInt1;
                params["vga"] = shmCmd->paramInt2;
                params["amp"] = (shmCmd->paramInt3 != 0);
            } else if (cmd == "set_source") {
                params["source"] = std::string(shmCmd->paramStr);
            }

            customCmdHandler(cmd, params);
            MemoryBarrier();
            shmCmd->ackSeq = shmCmd->cmdSeq;
        }
#endif
    }
}
