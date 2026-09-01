#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "flrc_config.h"

namespace flrc {

struct DecodedFrame {
    double timestamp;                  // System time in seconds
    double freqOffset;                 // Carrier frequency offset (Hz)
    uint32_t syncWord;                 // Recovered 32-bit sync word
    uint8_t mask;                      // Applied XOR mask (0x66, 0x99, 0x00, etc.)
    std::vector<uint8_t> rawCoded;     // Raw coded bytes
    std::vector<uint8_t> payload;      // Recovered plaintext payload bytes
    uint32_t hwCrc;                    // 32-bit hardware CRC extracted from frame
    bool crcValid;                     // Hardware CRC check passed
    float score;                       // AGC preamble correlation score
    std::string summary;               // Short description of frame
};

class FrameDetector {
public:
    FrameDetector();
    ~FrameDetector();

    void setConfig(const DemodConfig& demodCfg, const FramingConfig& framingCfg);
    void reset();

    // Ingest discriminator float samples and detect complete frames
    std::vector<DecodedFrame> processSamples(const float* samples, int count);

    // SX1280 CRC-32 calculation utility
    static uint32_t calculateSX1280CRC(const uint8_t* data, size_t length, uint32_t init = 0x00000000);

private:
    void detectFrames();
    bool decodeFrameAt(int startIdx, DecodedFrame& outFrame);

    DemodConfig _demodCfg;
    FramingConfig _framingCfg;

    std::vector<float> _sampleBuffer;
    std::vector<uint8_t> _bitBuffer;

    // Precomputed timing preamble pattern
    static const uint32_t TIMING_PREAMBLE_VAL = 0x043EE2 & 0x1FFFFF; // 21-bit
};

} // namespace flrc
