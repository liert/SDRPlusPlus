#include "protocol_h12.h"
#include <imgui.h>
#include <gui/style.h>
#include <iomanip>
#include <sstream>

namespace flrc {

H12ProtocolHandler::H12ProtocolHandler() {}

void H12ProtocolHandler::reset() {
    lastParsed = H12ParsedData();
    hasData = false;
    totalFrames = 0;
    validCrc8Frames = 0;
    pairingFrames = 0;
    mgmtFrames = 0;
}

uint8_t H12ProtocolHandler::crc8_smbus(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

H12ParsedData H12ProtocolHandler::parse(const std::vector<uint8_t>& payload) {
    H12ParsedData result;
    if (payload.empty()) return result;

    // Check for Pairing Request (42 bytes, magic 11 22 33 44 55)
    if (payload.size() >= 42 &&
        payload[0] == 0x11 && payload[1] == 0x22 && payload[2] == 0x33 &&
        payload[3] == 0x44 && payload[4] == 0x55) {
        result.frameType = H12FrameType::PAIRING_REQUEST;
        result.pairingId = ((uint32_t)payload[5] << 24) | ((uint32_t)payload[6] << 16) |
                           ((uint32_t)payload[7] << 8) | ((uint32_t)payload[8]);
        result.hopTable.assign(payload.begin() + 9, payload.begin() + 24);
        return result;
    }

    // Check for Pairing ACK (32 bytes, magic 66 77 88 99 AA)
    if (payload.size() >= 32 &&
        payload[0] == 0x66 && payload[1] == 0x77 && payload[2] == 0x88 &&
        payload[3] == 0x99 && payload[4] == 0xAA) {
        result.frameType = H12FrameType::PAIRING_ACK;
        return result;
    }

    // Check for Maintenance frames
    if (payload.size() >= 32) {
        if (payload[0] == 0xC3) {
            result.frameType = H12FrameType::MAINTENANCE_CMD;
            result.maintenanceCmd = payload[1];
            return result;
        } else if (payload[0] == 0xAA) {
            result.frameType = H12FrameType::MAINTENANCE_DATA;
            return result;
        } else if (payload[0] == 0xA5) {
            result.frameType = H12FrameType::MAINTENANCE_RESP;
            return result;
        }
    }

    // Normal 32-byte Control / Management Frame
    if (payload.size() >= 32) {
        uint8_t b0 = payload[0];
        result.hopIndex = (b0 >> 4) & 0x0F;
        result.groupIndex = (b0 >> 2) & 0x03;
        result.isManagement = (b0 >> 1) & 0x01;
        result.transparentRoute = b0 & 0x01;

        result.frameType = result.isManagement ? H12FrameType::NORMAL_MANAGEMENT : H12FrameType::NORMAL_CONTROL;

        // Channel Unpacking (12 channels, 10-bit each)
        result.channels[0] = ((uint16_t)payload[1] << 2) | (payload[2] >> 6);
        result.channels[1] = (((uint16_t)payload[2] & 0x3F) << 4) | (payload[3] >> 4);
        result.channels[2] = (((uint16_t)payload[3] & 0x0F) << 6) | (payload[4] >> 2);
        result.channels[3] = (((uint16_t)payload[4] & 0x03) << 8) | payload[5];
        for (int ch = 4; ch < 12; ch++) {
            result.channels[ch] = (uint16_t)payload[ch + 2] << 2;
        }

        result.channelsValid = true;
        for (int ch = 0; ch < 12; ch++) {
            if (result.channels[ch] > 1023) {
                result.channelsValid = false;
                break;
            }
        }

        // Transparent Data Extraction
        uint8_t transLen = payload[30] & 0x0F;
        if (transLen > 13) transLen = 13;
        for (int i = 0; i < 13; i++) {
            uint8_t byteVal = payload[14 + i];
            if (i < transLen) {
                result.transparentHex.push_back(byteVal);
            }
            if (byteVal >= 32 && byteVal <= 126) {
                result.transparentAscii += (char)byteVal;
            } else if (i < transLen) {
                result.transparentAscii += '.';
            }
        }

        // CRC-8 SMBUS over byte0..30
        result.crc8Calculated = crc8_smbus(payload.data(), 31);
        result.crc8Received = payload[31];
        result.crc8Valid = (result.crc8Calculated == result.crc8Received);
    }

    return result;
}

void H12ProtocolHandler::processFrame(const DecodedFrame& frame) {
    lastParsed = parse(frame.payload);
    hasData = true;
    totalFrames++;

    if (lastParsed.crc8Valid) validCrc8Frames++;
    if (lastParsed.frameType == H12FrameType::PAIRING_REQUEST || lastParsed.frameType == H12FrameType::PAIRING_ACK) {
        pairingFrames++;
    }
    if (lastParsed.isManagement) mgmtFrames++;
}

void H12ProtocolHandler::renderUI() {
    ImGui::Text("H12 Stats: Total: %llu | CRC8 Valid: %llu | Pairing: %llu | Mgmt: %llu",
                (unsigned long long)totalFrames, (unsigned long long)validCrc8Frames,
                (unsigned long long)pairingFrames, (unsigned long long)mgmtFrames);
    ImGui::Separator();

    if (!hasData) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Waiting for H12 air-protocol frames...");
        return;
    }

    // Frame Type & Link Status
    const char* typeStr = "Unknown";
    ImVec4 typeColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    switch (lastParsed.frameType) {
    case H12FrameType::NORMAL_CONTROL:
        typeStr = "Normal RC Control (32B)";
        typeColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        break;
    case H12FrameType::NORMAL_MANAGEMENT:
        typeStr = "Management Frame (32B)";
        typeColor = ImVec4(0.3f, 0.8f, 1.0f, 1.0f);
        break;
    case H12FrameType::PAIRING_REQUEST:
        typeStr = "Pairing Request (42B)";
        typeColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        break;
    case H12FrameType::PAIRING_ACK:
        typeStr = "Pairing ACK (32B)";
        typeColor = ImVec4(0.9f, 0.5f, 1.0f, 1.0f);
        break;
    case H12FrameType::MAINTENANCE_CMD:
    case H12FrameType::MAINTENANCE_DATA:
    case H12FrameType::MAINTENANCE_RESP:
        typeStr = "Maintenance Protocol";
        typeColor = ImVec4(1.0f, 0.5f, 0.3f, 1.0f);
        break;
    default:
        break;
    }

    ImGui::Text("Frame Type:");
    ImGui::SameLine();
    ImGui::TextColored(typeColor, "%s", typeStr);

    if (lastParsed.frameType == H12FrameType::NORMAL_CONTROL || lastParsed.frameType == H12FrameType::NORMAL_MANAGEMENT) {
        ImGui::Text("Hop Index: %u (CH#%u) | Group: %u | Route: %s | CRC8: ",
                    lastParsed.hopIndex, lastParsed.hopIndex + 1, lastParsed.groupIndex,
                    lastParsed.transparentRoute ? "Secondary" : "Primary");
        ImGui::SameLine();
        if (lastParsed.crc8Valid) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "0x%02X [OK]", lastParsed.crc8Received);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "0x%02X != 0x%02X [ERR]", lastParsed.crc8Received, lastParsed.crc8Calculated);
        }

        ImGui::Spacing();
        ImGui::Text("RC Channels (0..960, Mid=480):");
        const char* chNames[12] = {
            "CH1 (Roll)", "CH2 (Pitch)", "CH3 (Thr)", "CH4 (Yaw)",
            "CH5 (AUX1)", "CH6 (AUX2)", "CH7 (AUX3)", "CH8 (AUX4)",
            "CH9 (AUX5)", "CH10 (AUX6)", "CH11 (AUX7)", "CH12 (AUX8)"
        };

        // Render 2 columns for channels
        ImGui::Columns(2, "h12_ch_cols", false);
        for (int ch = 0; ch < 12; ch++) {
            uint16_t val = lastParsed.channels[ch];
            float frac = std::clamp((float)val / 960.0f, 0.0f, 1.0f);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s: %u", chNames[ch], val);
            ImGui::ProgressBar(frac, ImVec2(-1.0f, 14.0f), buf);
            if (ch == 5) ImGui::NextColumn();
        }
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Text("Transparent Telemetry:");
        if (!lastParsed.transparentAscii.empty()) {
            ImGui::Text("  ASCII: \"%s\"", lastParsed.transparentAscii.c_str());
        }
        if (!lastParsed.transparentHex.empty()) {
            std::stringstream ss;
            for (uint8_t b : lastParsed.transparentHex) {
                ss << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int)b << " ";
            }
            ImGui::Text("  HEX:   %s", ss.str().c_str());
        }
    } else if (lastParsed.frameType == H12FrameType::PAIRING_REQUEST) {
        ImGui::Text("Remote Identity: 0x%08X", lastParsed.pairingId);
        std::stringstream ss;
        for (uint8_t code : lastParsed.hopTable) {
            ss << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int)code << " ";
        }
        ImGui::Text("Hop Sequence: %s", ss.str().c_str());
    }
}

} // namespace flrc
