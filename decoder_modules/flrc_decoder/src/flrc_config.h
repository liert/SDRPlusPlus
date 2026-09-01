#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "i18n.h"

namespace flrc {

enum class PresetType {
    SX1280_FLRC_1300K,   // 1.3 Mbps, dev 325kHz, BT_DIS (Standard H12 FLRC)
    SX1280_FLRC_520K,    // 520 kbps, dev 130kHz
    SX1280_FLRC_260K,    // 260 kbps, dev 65kHz
    GENERIC_2FSK_CUSTOM  // Custom 2FSK/GFSK parameters
};

enum class MaskMode {
    AUTO_66_99, // Try both 0x66 and 0x99
    FIXED_66,   // Only 0x66
    FIXED_99,   // Only 0x99
    NONE_00,    // No mask (0x00)
    CUSTOM      // User defined mask
};

enum class ProtocolType {
    H12_DRONE_RC,  // H12 / T12 Drone Telemetry & RC Channels
    FLRC_GENERIC,  // Generic SX1280 FLRC Packet
    RAW_BITS       // Raw bitstream & hex dump
};

struct DemodConfig {
    double sampleRate = 8000000.0;    // DSP input samplerate from VFO
    double symbolRate = 1300000.0;    // Symbol rate / bitrate (e.g. 1.3 Mbps)
    double deviation = 325000.0;      // Frequency deviation
    double filterCutoff = 750000.0;   // Lowpass filter cutoff (Hz)
    int filterTaps = 129;             // FIR filter taps
    double energyThreshold = 0.005;   // Burst / Squelch threshold
    double agcScoreThreshold = 3.0;   // Preamble correlation score threshold
    bool enableBurstDetection = true; // Use energy burst gating
};

struct FramingConfig {
    bool enableAgcPreamble = true;      // Check 32-bit 0101... AGC preamble
    int agcPreambleBits = 32;
    bool enableTimingPreamble = true;   // Check 21-bit timing preamble (0x043EE2)
    int timingToleranceBits = 3;        // Max bit errors allowed in timing preamble (3 bits)
    bool autoSyncWord = true;           // Auto capture any valid sync word (Recommended)
    uint32_t syncWord = 0x54313253;     // Default sync word (e.g. 'T12S' = 0x54313253)
    bool syncWordMasked = true;         // Sync word is masked on air (SX1280 standard)
    bool differentialDecode = true;     // CumXOR differential decoding
    MaskMode maskMode = MaskMode::AUTO_66_99;
    uint8_t customMask = 0x66;
    bool enableHwCrc = true;            // Check SX1280 32-bit hardware CRC
    int defaultPayloadLen = 32;         // Normal payload length (32 bytes)
    int maxPayloadLen = 127;            // Max payload length (127 bytes)
};

inline void applyPreset(PresetType preset, DemodConfig& demod, FramingConfig& framing) {
    switch (preset) {
    case PresetType::SX1280_FLRC_1300K:
        demod.symbolRate = 1300000.0;
        demod.deviation = 325000.0;
        demod.filterCutoff = 750000.0;
        framing.enableAgcPreamble = true;
        framing.enableTimingPreamble = true;
        framing.timingToleranceBits = 3;
        framing.autoSyncWord = true;
        framing.syncWord = 0x54313253;
        framing.differentialDecode = true;
        framing.maskMode = MaskMode::AUTO_66_99;
        framing.enableHwCrc = true;
        framing.defaultPayloadLen = 32;
        break;
    case PresetType::SX1280_FLRC_520K:
        demod.symbolRate = 520000.0;
        demod.deviation = 130000.0;
        demod.filterCutoff = 300000.0;
        framing.enableAgcPreamble = true;
        framing.enableTimingPreamble = true;
        framing.timingToleranceBits = 3;
        framing.autoSyncWord = true;
        framing.syncWord = 0x54313253;
        framing.differentialDecode = true;
        framing.maskMode = MaskMode::AUTO_66_99;
        framing.enableHwCrc = true;
        framing.defaultPayloadLen = 32;
        break;
    case PresetType::SX1280_FLRC_260K:
        demod.symbolRate = 260000.0;
        demod.deviation = 65000.0;
        demod.filterCutoff = 150000.0;
        framing.enableAgcPreamble = true;
        framing.enableTimingPreamble = true;
        framing.timingToleranceBits = 3;
        framing.autoSyncWord = true;
        framing.syncWord = 0x54313253;
        framing.differentialDecode = true;
        framing.maskMode = MaskMode::AUTO_66_99;
        framing.enableHwCrc = true;
        framing.defaultPayloadLen = 32;
        break;
    case PresetType::GENERIC_2FSK_CUSTOM:
        break;
    }
}

} // namespace flrc
