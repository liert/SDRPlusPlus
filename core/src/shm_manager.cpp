#include "shm_manager.h"
#include <utils/flog.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>

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

    bool init() {
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

        flog::info("⚡ Windows Native Shared Memory (IPC) Engine Initialized (Buffer: 0x{0:X})", (uintptr_t)shmHeader);
        return true;
#else
        return false;
#endif
    }

    void cleanup() {
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
            // parse hex bytes into p.payload
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
