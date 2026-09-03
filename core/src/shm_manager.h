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

#define SDRPP_MAX_FFT_SIZE 4096

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

struct ShmDeviceInfo {
    char serial[64];
    char name[64];
    int32_t index;
};

struct ShmHeader {
    uint32_t magic;          // 0x53445250
    uint32_t version;        // 1
    uint32_t seq;            // Atomic increment per FFT frame
    uint32_t fftSize;        // 512, 1024, 2048, 4096
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
    
    // Connected Hardware Devices
    uint32_t deviceCount;
    ShmDeviceInfo devices[8];

    // Decoded FLRC Packet Ring Buffer (last 32 packets)
    uint32_t packetSeq;      // Atomic increment per packet
    uint32_t packetWriteIdx; // 0..31
    ShmPacket packets[32];

    // Additional FFT Info
    uint32_t fftWindow;      // 0=Blackman-Harris, 1=Hann, 2=Hamming, 3=Blackman, 4=Nuttall, 5=Flat Top, 6=Rectangular
    uint32_t fftRate;        // e.g. 60 FPS

    // Loaded File Info
    uint8_t fileLoaded;      // 1 if file loaded, 0 if not
    char currentFile[255];   // Currently mounted IQ file path

    // Real-time FFT Power Spectrum (up to 4096 bins, float in dBm)
    float fftData[SDRPP_MAX_FFT_SIZE];
};

struct ShmCmdBuffer {
    uint32_t cmdSeq;         // Increment when client sends command
    uint32_t ackSeq;         // Backend sets ackSeq = cmdSeq when done
    char cmd[32];            // "start", "stop", "set_freq", "set_gain", "set_fft", etc.
    double paramDouble;      // freq / sampleRate
    int32_t paramInt1;       // lna / fftSize
    int32_t paramInt2;       // vga / fftWindow
    int32_t paramInt3;       // amp / biasT / fftRate
    char paramStr[64];       // source name / serial
    char paramPath[512];     // Full file path (for File Source)
};

namespace shm_manager {
    bool init();
    void cleanup();

    // High-performance IQ FFT processing into Shared Memory
    void processIqSamples(const dsp::complex_t* samples, int count, double sampleRate);
    void updateFft(const float* fftDb, int size);
    void pushPacket(const nlohmann::json& packet);
    void updateDevices();
    void updateState(bool running, const std::string& sourceName, double centerFreq, double sampleRate, int lna, int vga, bool amp, bool biasT, const std::string& serial, bool fileLoaded = false, const std::string& currentFile = "");
    void setFftParams(int fftSize, int windowType, int rate);

    typedef std::function<nlohmann::json(const std::string& cmd, const nlohmann::json& params)> CommandHandler;
    void setCommandHandler(CommandHandler handler);
    void checkCommands();
}
