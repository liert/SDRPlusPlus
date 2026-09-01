#pragma once
#include "protocol_base.h"
#include <imgui.h>
#include <iomanip>
#include <sstream>

namespace flrc {

class FLRCGenericProtocolHandler : public IProtocolHandler {
public:
    const char* getName() const override { return "SX1280 FLRC Generic"; }
    const char* getDescription() const override { return "Standard SX1280 FLRC physical frame structures"; }

    void processFrame(const DecodedFrame& frame) override {
        lastFrame = frame;
        hasFrame = true;
        validFrames += frame.crcValid ? 1 : 0;
        totalFrames++;
    }

    void renderUI() override {
        ImGui::Text("Total FLRC Frames: %llu | Valid CRC: %llu", (unsigned long long)totalFrames, (unsigned long long)validFrames);
        ImGui::Separator();

        if (!hasFrame) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Waiting for SX1280 FLRC frames...");
            return;
        }

        ImGui::Text("Sync Word: 0x%08X", lastFrame.syncWord);
        ImGui::SameLine();
        ImGui::Text("| Mask: 0x%02X", lastFrame.mask);
        ImGui::SameLine();
        if (lastFrame.crcValid) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[CRC OK]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[CRC FAIL]");
        }

        ImGui::Text("Hardware CRC: 0x%08X | Payload Length: %zu Bytes", lastFrame.hwCrc, lastFrame.payload.size());

        ImGui::Spacing();
        ImGui::Text("Decoded Payload Hex:");
        std::stringstream ss;
        for (size_t i = 0; i < lastFrame.payload.size(); i++) {
            ss << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int)lastFrame.payload[i] << " ";
            if ((i + 1) % 16 == 0 || i + 1 == lastFrame.payload.size()) {
                ImGui::Text("  %s", ss.str().c_str());
                ss.str("");
                ss.clear();
            }
        }
    }

    void reset() override {
        hasFrame = false;
        totalFrames = 0;
        validFrames = 0;
    }

private:
    bool hasFrame = false;
    DecodedFrame lastFrame;
    uint64_t totalFrames = 0;
    uint64_t validFrames = 0;
};

} // namespace flrc
