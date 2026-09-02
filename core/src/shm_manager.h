#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include <json.hpp>
#include <dsp/types.h>

#define SDRPP_SHM_MAGIC 0x53445250 // "SDRP"
#define SDRPP_SHM_NAME L"Local\\SDRPP_SHM_BUFFER"
#define SDRPP_SHM_CMD_NAME L"Local\\SDRPP_SHM_CMD"

struct ShmPacket {
    uint32_t id;
    uint32_t syncWord;
    uint8_t mask;
    uint8_t crcValid;
    uint16_t payloadLen;
    uint32_t hwCrc;
    float freqOffsetKhz;
    char timestamp[32];
    uint8_t payload[128];
};

struct ShmHeader {
    uint32_t magic;          // 0x53445250
    uint32_t version;        // 1
    uint32_t seq;            // Atomic increment per FFT frame
    uint32_t fftSize;        // 1024
    double sampleRate;       // 8000000.0
    double centerFreq;       // 2400000000.0
    int32_t lnaGain;
    int32_t vgaGain;
    uint8_t ampEnable;
    uint8_t biasTEnable;
    uint8_t running;
    uint8_t sourceId;
    char sourceName[32];
    char deviceSerial[64];
    
    // Decoded FLRC Packet Ring Buffer (last 32 packets)
    uint32_t packetSeq;      // Atomic increment per packet
    uint32_t packetWriteIdx; // 0..31
    ShmPacket packets[32];

    // Real-time FFT Power Spectrum (1024 bins, float in dBm)
    float fftData[1024];
};

struct ShmCmdBuffer {
    uint32_t cmdSeq;         // Increment when client sends command
    uint32_t ackSeq;         // Backend sets ackSeq = cmdSeq when done
    char cmd[32];            // "start", "stop", "set_freq", "set_gain", etc.
    double paramDouble;      // freq / sampleRate
    int32_t paramInt1;       // lna
    int32_t paramInt2;       // vga
    int32_t paramInt3;       // amp / biasT
    char paramStr[64];       // source name / serial
};

namespace shm_manager {
    bool init();
    void cleanup();

    void updateFft(const float* fftDb, int size);
    void pushPacket(const nlohmann::json& packet);
    void updateState(bool running, const std::string& sourceName, double centerFreq, double sampleRate, int lna, int vga, bool amp, bool biasT, const std::string& serial);

    typedef std::function<nlohmann::json(const std::string& cmd, const nlohmann::json& params)> CommandHandler;
    void setCommandHandler(CommandHandler handler);
    void checkCommands();
}
