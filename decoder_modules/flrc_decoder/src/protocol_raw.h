#pragma once
#include "protocol_base.h"
#include <imgui.h>
#include <gui/style.h>
#include <iomanip>
#include <sstream>

namespace flrc {

class RawProtocolHandler : public IProtocolHandler {
public:
    const char* getName() const override { return "Raw Bitstream / Hex"; }
    const char* getDescription() const override { return "Generic hex and ASCII payload inspector"; }

    void processFrame(const DecodedFrame& frame) override {
        lastPayload = frame.payload;
        totalFrames++;
        totalBytes += frame.payload.size();
    }

    void renderUI() override {
        ImGui::Text("Frames: %llu | Total Bytes: %llu", (unsigned long long)totalFrames, (unsigned long long)totalBytes);
        ImGui::Separator();

        if (lastPayload.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No frames received yet...");
            return;
        }

        ImGui::Text("Last Payload Size: %zu Bytes", lastPayload.size());

        // Hex Dump View
        std::stringstream hexStream;
        std::stringstream asciiStream;
        for (size_t i = 0; i < lastPayload.size(); i++) {
            hexStream << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (int)lastPayload[i] << " ";
            char c = (lastPayload[i] >= 32 && lastPayload[i] <= 126) ? (char)lastPayload[i] : '.';
            asciiStream << c;
            if ((i + 1) % 16 == 0 || i + 1 == lastPayload.size()) {
                ImGui::Text("%04X  %-48s  |%s|", (int)(i & ~0x0F), hexStream.str().c_str(), asciiStream.str().c_str());
                hexStream.str("");
                hexStream.clear();
                asciiStream.str("");
                asciiStream.clear();
            }
        }
    }

    void reset() override {
        lastPayload.clear();
        totalFrames = 0;
        totalBytes = 0;
    }

private:
    std::vector<uint8_t> lastPayload;
    uint64_t totalFrames = 0;
    uint64_t totalBytes = 0;
};

} // namespace flrc
