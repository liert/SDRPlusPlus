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

#if __has_include(<libhackrf/hackrf.h>)
#include <libhackrf/hackrf.h>
#elif __has_include(<hackrf.h>)
#include <hackrf.h>
#endif

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
    static int currentFftSize = 1024;
    static int currentWindowType = 0; // 0=Blackman-Harris, 1=Hann, 2=Hamming, 3=Blackman, 4=Nuttall, 5=Flat Top, 6=Rectangular
    static int currentFftRate = 60;   // FPS
    static fftwf_complex* fftwIn = nullptr;
    static fftwf_complex* fftwOut = nullptr;
    static fftwf_plan fftPlan = nullptr;
    static float windowLut[SDRPP_MAX_FFT_SIZE];
    static float fftOutputDb[SDRPP_MAX_FFT_SIZE];
    static std::mutex fftMutex;
    static std::chrono::steady_clock::time_point lastFftTime;

    static void computeWindowLut(int size, int windowType) {
        if (size <= 0 || size > SDRPP_MAX_FFT_SIZE) size = 1024;
        for (int i = 0; i < size; i++) {
            double f = (2.0 * M_PI * i) / (size - 1);
            switch (windowType) {
                case 0: { // Blackman-Harris 4-term (-92 dB sidelobes)
                    double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
                    windowLut[i] = (float)(a0 - a1 * cos(f) + a2 * cos(2.0 * f) - a3 * cos(3.0 * f));
                    break;
                }
                case 1: { // Hann
                    windowLut[i] = (float)(0.5 - 0.5 * cos(f));
                    break;
                }
                case 2: { // Hamming
                    windowLut[i] = (float)(0.54 - 0.46 * cos(f));
                    break;
                }
                case 3: { // Blackman (3-term)
                    windowLut[i] = (float)(0.42 - 0.5 * cos(f) + 0.08 * cos(2.0 * f));
                    break;
                }
                case 4: { // Nuttall
                    windowLut[i] = (float)(0.355768 - 0.487396 * cos(f) + 0.144232 * cos(2.0 * f) - 0.012604 * cos(3.0 * f));
                    break;
                }
                case 5: { // Flat Top (High amplitude accuracy)
                    windowLut[i] = (float)(0.21557895 - 0.41663158 * cos(f) + 0.277263158 * cos(2.0 * f) - 0.083578947 * cos(3.0 * f) + 0.006947368 * cos(4.0 * f));
                    break;
                }
                case 6: { // Rectangular (None)
                    windowLut[i] = 1.0f;
                    break;
                }
                default: { // Fallback
                    double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
                    windowLut[i] = (float)(a0 - a1 * cos(f) + a2 * cos(2.0 * f) - a3 * cos(3.0 * f));
                    break;
                }
            }
        }
    }

    static void initFft(int size = 1024, int windowType = 0, int rate = 60) {
        std::lock_guard<std::mutex> lock(fftMutex);
        if (size != 512 && size != 1024 && size != 2048 && size != 4096) {
            size = 1024;
        }
        currentFftSize = size;
        currentWindowType = windowType;
        currentFftRate = (rate >= 5 && rate <= 120) ? rate : 60;

        if (fftPlan) {
            fftwf_destroy_plan(fftPlan);
            fftPlan = nullptr;
        }
        if (fftwIn) {
            fftwf_free(fftwIn);
            fftwIn = nullptr;
        }
        if (fftwOut) {
            fftwf_free(fftwOut);
            fftwOut = nullptr;
        }

        fftwIn = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * currentFftSize);
        fftwOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * currentFftSize);
        fftPlan = fftwf_plan_dft_1d(currentFftSize, fftwIn, fftwOut, FFTW_FORWARD, FFTW_ESTIMATE);

        computeWindowLut(currentFftSize, currentWindowType);
        lastFftTime = std::chrono::steady_clock::now();

#ifdef _WIN32
        if (shmHeader) {
            shmHeader->fftSize = currentFftSize;
            shmHeader->fftWindow = currentWindowType;
            shmHeader->fftRate = currentFftRate;
        }
#endif
    }

    static void cleanupFft() {
        std::lock_guard<std::mutex> lock(fftMutex);
        if (fftPlan) { fftwf_destroy_plan(fftPlan); fftPlan = nullptr; }
        if (fftwIn) { fftwf_free(fftwIn); fftwIn = nullptr; }
        if (fftwOut) { fftwf_free(fftwOut); fftwOut = nullptr; }
    }

    void setFftParams(int fftSize, int windowType, int rate) {
        initFft(fftSize, windowType, rate);
        flog::info("⚡ [FFT Config] Reconfigured FFT: Size={0}, Window={1}, Rate={2} FPS", currentFftSize, currentWindowType, currentFftRate);
    }

    void updateDevices() {
#ifdef _WIN32
        if (!shmHeader) return;
        std::lock_guard<std::mutex> lock(shmMtx);
#if defined(LIBHACKRF_HACKRF_H) || defined(__HACKRF_H__) || defined(HACKRF_SUCCESS)
        try {
            hackrf_device_list_t* list = hackrf_device_list();
            if (list) {
                shmHeader->deviceCount = (uint32_t)std::min<int>(list->devicecount, 8);
                for (int i = 0; i < (int)shmHeader->deviceCount; i++) {
                    if (list->serial_numbers[i]) {
                        std::strncpy(shmHeader->devices[i].serial, list->serial_numbers[i], 63);
                        std::string s(list->serial_numbers[i]);
                        std::string shortSerial = (s.length() >= 16) ? s.substr(16) : s;
                        snprintf(shmHeader->devices[i].name, 63, "HackRF One (%s)", shortSerial.c_str());
                        shmHeader->devices[i].index = i;
                    }
                }
                hackrf_device_list_free(list);
            } else {
                shmHeader->deviceCount = 0;
            }
        } catch (...) {
            shmHeader->deviceCount = 0;
        }
#endif
#endif
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
        shmHeader->fftSize = currentFftSize;
        shmHeader->fftWindow = currentWindowType;
        shmHeader->fftRate = currentFftRate;
        shmHeader->sampleRate = 8000000.0;
        shmHeader->centerFreq = 2400000000.0;
        shmHeader->lnaGain = 32;
        shmHeader->vgaGain = 20;
        shmHeader->running = 0;
        shmHeader->seq = 0;
        for (int i = 0; i < SDRPP_MAX_FFT_SIZE; i++) {
            shmHeader->fftData[i] = -120.0f; // Silence / Noise floor baseline
        }

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
        if (!samples || count < currentFftSize || !fftPlan) return;

        // Rate limit FFT to configured rate (e.g. 60 FPS -> 16ms, 30 FPS -> 33ms)
        auto now = std::chrono::steady_clock::now();
        int intervalMs = std::max(5, 1000 / currentFftRate);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFftTime).count();
        if (elapsedMs < intervalMs) return;
        lastFftTime = now;

        std::unique_lock<std::mutex> lock(fftMutex, std::try_to_lock);
        if (!lock.owns_lock()) return;

        int n = currentFftSize;
        int maxOffset = count - n;

        // Smart burst detection: scan chunks to capture burst packets in this frame
        int startIdx = 0;
        float maxChunkPwr = -1.0f;
        int scanStep = std::max(512, n / 2);
        for (int offset = 0; offset <= maxOffset; offset += scanStep) {
            float pwrSum = 0.0f;
            // Sample a fast subset
            for (int s = 0; s < n; s += 8) {
                float r = samples[offset + s].re;
                float m = samples[offset + s].im;
                pwrSum += r * r + m * m;
            }
            if (pwrSum > maxChunkPwr) {
                maxChunkPwr = pwrSum;
                startIdx = offset;
            }
        }

        // DC offset removal across chosen window to eliminate 0 Hz hardware LO leakage
        float dcI = 0.0f;
        float dcQ = 0.0f;
        for (int i = 0; i < n; i++) {
            dcI += samples[startIdx + i].re;
            dcQ += samples[startIdx + i].im;
        }
        dcI /= (float)n;
        dcQ /= (float)n;

        // Apply DC blocking and window, copy into FFTW input
        for (int i = 0; i < n; i++) {
            fftwIn[i][0] = (samples[startIdx + i].re - dcI) * windowLut[i];
            fftwIn[i][1] = (samples[startIdx + i].im - dcQ) * windowLut[i];
        }

        fftwf_execute(fftPlan);

        // Compute power in dBm with proper 1/N normalization
        int halfFft = n / 2;
        float norm = 1.0f / (float)n;
        for (int i = 0; i < n; i++) {
            int srcIdx = (i + halfFft) % n;
            float re = fftwOut[srcIdx][0] * norm;
            float im = fftwOut[srcIdx][1] * norm;
            float pwr = re * re + im * im + 1e-12f;
            float dbm = 10.0f * log10f(pwr);
            fftOutputDb[i] = std::max(-140.0f, std::min(20.0f, dbm));
        }

        // Direct Ultra-Fast write into Windows Shared Memory (< 0.001 ms)
        updateFft(fftOutputDb, n);

        static uint64_t fftLogCounter = 0;
        if (++fftLogCounter % 120 == 0) {
            float minP = 100.0f, maxP = -150.0f;
            for (int i = 0; i < n; i++) {
                if (fftOutputDb[i] < minP) minP = fftOutputDb[i];
                if (fftOutputDb[i] > maxP) maxP = fftOutputDb[i];
            }
            flog::info("📊 [DSP FFT Pipeline] Active: {0} frames computed (N={1}, seq={2}), Range: {3} dBm ~ {4} dBm",
                       fftLogCounter, n, (uint32_t)shmHeader->seq, (int)minP, (int)maxP);
        }
    }

    void updateFft(const float* fftDb, int size) {
#ifdef _WIN32
        if (!shmHeader || !fftDb || size <= 0) return;
        int copySize = std::min(size, SDRPP_MAX_FFT_SIZE);
        shmHeader->fftSize = copySize;
        shmHeader->fftWindow = currentWindowType;
        shmHeader->fftRate = currentFftRate;
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

    void updateState(bool running, const std::string& sourceName, double centerFreq, double sampleRate, int lna, int vga, bool amp, bool biasT, const std::string& serial, bool fileLoaded, const std::string& currentFile) {
#ifdef _WIN32
        if (!shmHeader) return;
        shmHeader->running = running ? 1 : 0;
        shmHeader->centerFreq = centerFreq;
        shmHeader->sampleRate = sampleRate;
        shmHeader->lnaGain = lna;
        shmHeader->vgaGain = vga;
        shmHeader->ampEnable = amp ? 1 : 0;
        shmHeader->biasTEnable = biasT ? 1 : 0;
        shmHeader->fileLoaded = fileLoaded ? 1 : 0;
        strncpy(shmHeader->sourceName, sourceName.c_str(), sizeof(shmHeader->sourceName) - 1);
        if (!serial.empty()) {
            strncpy(shmHeader->deviceSerial, serial.c_str(), sizeof(shmHeader->deviceSerial) - 1);
        }
        if (!currentFile.empty()) {
            strncpy(shmHeader->currentFile, currentFile.c_str(), sizeof(shmHeader->currentFile) - 1);
        }
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
                if (shmCmd->paramPath[0] != 0) {
                    params["path"] = std::string(shmCmd->paramPath);
                }
            } else if (cmd == "set_file_path") {
                if (shmCmd->paramPath[0] != 0) {
                    params["path"] = std::string(shmCmd->paramPath);
                }
                params["loop"] = (shmCmd->paramInt1 != 0);
            } else if (cmd == "set_fft" || cmd == "set_fft_params") {
                if (shmCmd->paramInt1 > 0) params["fftSize"] = shmCmd->paramInt1;
                if (shmCmd->paramInt2 >= 0) params["fftWindow"] = shmCmd->paramInt2;
                if (shmCmd->paramInt3 > 0) params["fftRate"] = shmCmd->paramInt3;
            } else if (cmd == "get_devices" || cmd == "refresh_devices") {
                updateDevices();
            }

            customCmdHandler(cmd, params);
            MemoryBarrier();
            shmCmd->ackSeq = shmCmd->cmdSeq;
        }
#endif
    }
}
