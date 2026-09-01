#include "frame_detector.h"
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace flrc {

FrameDetector::FrameDetector() {
    _sampleBuffer.reserve(131072);
}

FrameDetector::~FrameDetector() {}

void FrameDetector::setConfig(const DemodConfig& demodCfg, const FramingConfig& framingCfg) {
    _demodCfg = demodCfg;
    _framingCfg = framingCfg;
}

void FrameDetector::reset() {
    _sampleBuffer.clear();
    _bitBuffer.clear();
}

uint32_t FrameDetector::calculateSX1280CRC(const uint8_t* data, size_t length, uint32_t init) {
    uint32_t crc = init;
    for (size_t i = 0; i < length; i++) {
        crc ^= ((uint32_t)data[i]) << 24;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ 0x04C11DB7;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static int countBitErrors(uint32_t a, uint32_t b, int bits) {
    uint32_t diff = (a ^ b) & ((1U << bits) - 1);
    int count = 0;
    while (diff) {
        count += (diff & 1);
        diff >>= 1;
    }
    return count;
}

std::vector<DecodedFrame> FrameDetector::processSamples(const float* samples, int count) {
    std::vector<DecodedFrame> frames;
    if (count <= 0) return frames;

    // Append new samples to buffer
    _sampleBuffer.insert(_sampleBuffer.end(), samples, samples + count);

    double samplesPerBit = _demodCfg.sampleRate / _demodCfg.symbolRate;
    if (samplesPerBit <= 0.0) samplesPerBit = 6.1538;

    // Minimum samples needed for one full frame (~ 1200 bits)
    int minSamplesNeeded = (int)(samplesPerBit * (32 + 21 + 32 + (_framingCfg.defaultPayloadLen + 4) * 8 + 64));

    if ((int)_sampleBuffer.size() < minSamplesNeeded) {
        return frames;
    }

    // Process in windows
    int maxSearchIdx = (int)_sampleBuffer.size() - minSamplesNeeded;
    int halfStep = (int)(samplesPerBit / 2);
    int step = (halfStep > 1) ? halfStep : 1;

    for (int i = 0; i < maxSearchIdx; i += step) {
        DecodedFrame frame;
        if (decodeFrameAt(i, frame)) {
            frames.push_back(frame);
            // Advance search position past the current frame
            int frameLenSamples = (int)(samplesPerBit * (32 + 21 + 32 + (frame.payload.size() + 4) * 8));
            i += (step > frameLenSamples) ? step : frameLenSamples;
        }
    }

    // Retain only unconsumed trailing samples
    if (_sampleBuffer.size() > 65536) {
        int retain = (int)_sampleBuffer.size();
        if (retain > minSamplesNeeded * 2) {
            retain = minSamplesNeeded * 2;
        }
        _sampleBuffer.erase(_sampleBuffer.begin(), _sampleBuffer.end() - retain);
    }

    return frames;
}

bool FrameDetector::decodeFrameAt(int startIdx, DecodedFrame& outFrame) {
    double samplesPerBit = _demodCfg.sampleRate / _demodCfg.symbolRate;
    if (samplesPerBit <= 0.0) return false;

    // 1. AGC Preamble Correlation (32 bits alternating +1, -1)
    if (_framingCfg.enableAgcPreamble) {
        float correlation = 0.0f;
        float power = 0.0f;
        for (int b = 0; b < 32; b++) {
            int sIdx = startIdx + (int)(b * samplesPerBit);
            if (sIdx >= (int)_sampleBuffer.size()) return false;
            float val = _sampleBuffer[sIdx];
            float expected = (b % 2 == 0) ? -1.0f : 1.0f;
            correlation += val * expected;
            power += std::abs(val);
        }
        float score = (power > 0.001f) ? (std::abs(correlation) / (power + 0.01f)) * 4.0f : 0.0f;
        if (score < _demodCfg.agcScoreThreshold) {
            return false;
        }
        outFrame.score = score;
    }

    // 2. Extract Timing Preamble (21 bits)
    int timingStartIdx = startIdx + (int)(32 * samplesPerBit);
    uint32_t recoveredTiming = 0;
    for (int b = 0; b < 21; b++) {
        int sIdx = timingStartIdx + (int)(b * samplesPerBit);
        if (sIdx >= (int)_sampleBuffer.size()) return false;
        uint8_t bit = (_sampleBuffer[sIdx] > 0.0f) ? 1 : 0;
        recoveredTiming = (recoveredTiming << 1) | bit;
    }

    if (_framingCfg.enableTimingPreamble) {
        int errors = countBitErrors(recoveredTiming, TIMING_PREAMBLE_VAL, 21);
        if (errors > _framingCfg.timingToleranceBits) {
            // Also check inverted polarity
            int invErrors = countBitErrors((~recoveredTiming) & 0x1FFFFF, TIMING_PREAMBLE_VAL, 21);
            if (invErrors > _framingCfg.timingToleranceBits) {
                return false;
            }
        }
    }

    // 3. Extract Sync Word (32 bits)
    int syncStartIdx = timingStartIdx + (int)(21 * samplesPerBit);
    std::vector<uint8_t> syncBits(32);
    for (int b = 0; b < 32; b++) {
        int sIdx = syncStartIdx + (int)(b * samplesPerBit);
        if (sIdx >= (int)_sampleBuffer.size()) return false;
        syncBits[b] = (_sampleBuffer[sIdx] > 0.0f) ? 1 : 0;
    }

    // Differential decode on sync word (cumxor)
    uint32_t diffSync = 0;
    uint8_t prevBit = 0;
    for (int b = 0; b < 32; b++) {
        prevBit ^= syncBits[b];
        diffSync = (diffSync << 1) | prevBit;
    }

    // If not auto sync, check if diffSync matches configured syncWord (or with mask)
    if (!_framingCfg.autoSyncWord) {
        uint32_t targetSync = _framingCfg.syncWord;
        if (diffSync != targetSync && diffSync != (targetSync ^ 0x66666666) && diffSync != (targetSync ^ 0x99999999)) {
            return false;
        }
    }

    // 4. Extract Coded Payload & CRC Bits
    int codedStartIdx = syncStartIdx + (int)(32 * samplesPerBit);
    int maxBits = (_framingCfg.maxPayloadLen + 4) * 8;
    std::vector<uint8_t> codedBits;
    codedBits.reserve(maxBits);

    for (int b = 0; b < maxBits; b++) {
        int sIdx = codedStartIdx + (int)(b * samplesPerBit);
        if (sIdx >= (int)_sampleBuffer.size()) break;
        codedBits.push_back((_sampleBuffer[sIdx] > 0.0f) ? 1 : 0);
    }

    if (codedBits.size() < (size_t)((_framingCfg.defaultPayloadLen + 4) * 8)) {
        return false;
    }

    // 5. Differential Decode on Coded Payload
    std::vector<uint8_t> diffCodedBits(codedBits.size());
    uint8_t lastBit = 0;
    for (size_t b = 0; b < codedBits.size(); b++) {
        lastBit ^= codedBits[b];
        diffCodedBits[b] = lastBit;
    }

    // Pack into bytes
    size_t numBytes = diffCodedBits.size() / 8;
    std::vector<uint8_t> cumxorBytes(numBytes);
    for (size_t i = 0; i < numBytes; i++) {
        uint8_t byteVal = 0;
        for (int b = 0; b < 8; b++) {
            byteVal = (byteVal << 1) | diffCodedBits[i * 8 + b];
        }
        cumxorBytes[i] = byteVal;
    }

    // 6. Test Candidate Masks (0x99, 0x66, 0x00, custom)
    std::vector<uint8_t> candidateMasks;
    if (_framingCfg.maskMode == MaskMode::AUTO_66_99) {
        candidateMasks.push_back(0x99); // Normal transmitter default
        candidateMasks.push_back(0x66); // Normal receiver/pairing default
    } else if (_framingCfg.maskMode == MaskMode::FIXED_99) {
        candidateMasks.push_back(0x99);
    } else if (_framingCfg.maskMode == MaskMode::FIXED_66) {
        candidateMasks.push_back(0x66);
    } else if (_framingCfg.maskMode == MaskMode::NONE_00) {
        candidateMasks.push_back(0x00);
    } else if (_framingCfg.maskMode == MaskMode::CUSTOM) {
        candidateMasks.push_back(_framingCfg.customMask);
    }

    bool matched = false;
    for (uint8_t mask : candidateMasks) {
        std::vector<uint8_t> plainBytes(cumxorBytes.size());
        for (size_t i = 0; i < cumxorBytes.size(); i++) {
            plainBytes[i] = cumxorBytes[i] ^ mask;
        }

        // Determine actual payload length
        int payloadLen = _framingCfg.defaultPayloadLen;
        // Check for H12 pairing frame (42 bytes), extended fragment (127 bytes), or normal (32 bytes)
        if (plainBytes.size() >= 46 && plainBytes[0] == 0x11 && plainBytes[1] == 0x22 && plainBytes[2] == 0x33) {
            payloadLen = 42;
        } else if (plainBytes.size() >= 36 && plainBytes[0] == 0x5A) {
            payloadLen = ((int)plainBytes.size() - 4 < 127) ? (int)plainBytes.size() - 4 : 127;
        } else if (plainBytes.size() >= 36) {
            payloadLen = 32;
        }

        if ((int)plainBytes.size() < payloadLen + 4) continue;

        // Extract CRC
        uint32_t rxCrc = ((uint32_t)plainBytes[payloadLen] << 24) |
                         ((uint32_t)plainBytes[payloadLen + 1] << 16) |
                         ((uint32_t)plainBytes[payloadLen + 2] << 8) |
                         ((uint32_t)plainBytes[payloadLen + 3]);

        // Calculate expected CRC over Sync Word + Payload
        uint32_t actualSync = _framingCfg.autoSyncWord ? diffSync : _framingCfg.syncWord;
        uint8_t syncBuf[4] = {
            (uint8_t)(actualSync >> 24),
            (uint8_t)(actualSync >> 16),
            (uint8_t)(actualSync >> 8),
            (uint8_t)(actualSync)
        };
        std::vector<uint8_t> crcInput;
        crcInput.insert(crcInput.end(), syncBuf, syncBuf + 4);
        crcInput.insert(crcInput.end(), plainBytes.begin(), plainBytes.begin() + payloadLen);

        uint32_t calcCrc0 = calculateSX1280CRC(crcInput.data(), crcInput.size(), 0x00000000);
        uint32_t calcCrcF = calculateSX1280CRC(crcInput.data(), crcInput.size(), 0xFFFFFFFF);

        bool crcOk = (rxCrc == calcCrc0 || rxCrc == calcCrcF);

        if (crcOk || !_framingCfg.enableHwCrc) {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            outFrame.timestamp = std::chrono::duration<double>(duration).count();
            outFrame.syncWord = actualSync;
            outFrame.mask = mask;
            outFrame.rawCoded = cumxorBytes;
            outFrame.payload.assign(plainBytes.begin(), plainBytes.begin() + payloadLen);
            outFrame.hwCrc = rxCrc;
            outFrame.crcValid = crcOk;
            outFrame.freqOffset = 0.0;
            matched = true;
            break;
        }
    }

    return matched;
}

} // namespace flrc
