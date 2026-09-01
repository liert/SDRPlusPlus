#include <imgui.h>
#include <config.h>
#include <core.h>
#include <gui/style.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <module.h>
#include <dsp/sink/handler_sink.h>
#include <dsp/buffer/reshaper.h>
#include <gui/widgets/symbol_diagram.h>
#include <utils/optionlist.h>
#include <vector>
#include <deque>
#include <mutex>
#include <memory>
#include <chrono>

#include "flrc_config.h"
#include "flrc_dsp.h"
#include "frame_detector.h"
#include "protocol_base.h"
#include "protocol_raw.h"
#include "protocol_flrc.h"
#include "protocol_h12.h"

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "flrc_decoder",
    /* Description:     */ "Universal Demodulator and SX1280 FLRC / H12 Protocol Decoder",
    /* Author:          */ "DeepSeek Harness",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ -1
};

ConfigManager config;

namespace flrc {

class FLRCDecoderModule : public ModuleManager::Instance {
public:
    FLRCDecoderModule(std::string name) : diag(0.6, 1024) {
        this->name = name;

        // Init option lists
        presets.define(0, "SX1280 FLRC (1.3 Mbps, H12)", PresetType::SX1280_FLRC_1300K);
        presets.define(1, "SX1280 FLRC (520 kbps)", PresetType::SX1280_FLRC_520K);
        presets.define(2, "SX1280 FLRC (260 kbps)", PresetType::SX1280_FLRC_260K);
        presets.define(3, "Custom Generic 2FSK", PresetType::GENERIC_2FSK_CUSTOM);

        maskModes.define(0, "Auto (0x66 / 0x99)", MaskMode::AUTO_66_99);
        maskModes.define(1, "Fixed 0x66", MaskMode::FIXED_66);
        maskModes.define(2, "Fixed 0x99", MaskMode::FIXED_99);
        maskModes.define(3, "None (0x00)", MaskMode::NONE_00);
        maskModes.define(4, "Custom", MaskMode::CUSTOM);

        protocols.define(0, "H12 / T12 Drone Protocol", ProtocolType::H12_DRONE_RC);
        protocols.define(1, "SX1280 FLRC Generic", ProtocolType::FLRC_GENERIC);
        protocols.define(2, "Raw Bitstream / Hex", ProtocolType::RAW_BITS);

        // Register protocol handlers
        protoHandlers.push_back(std::make_unique<H12ProtocolHandler>());
        protoHandlers.push_back(std::make_unique<FLRCGenericProtocolHandler>());
        protoHandlers.push_back(std::make_unique<RawProtocolHandler>());

        // Initialize default configuration
        applyPreset(PresetType::SX1280_FLRC_1300K, demodCfg, framingCfg);

        // Initialize VFO (default 8 MHz sample rate, 2 MHz bandwidth)
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, 0, 2000000, 8000000, 500000, 10000000, false);
        vfo->setSnapInterval(1000);

        // Setup DSP chain
        dsp.init(vfo->output, demodCfg);
        detector.setConfig(demodCfg, framingCfg);

        dataSink.init(&dsp.out, _dspDataHandler, this);
        diagSink.init(&dsp.diagOut, _diagHandler, this);

        // Start processing
        dsp.start();
        dataSink.start();
        diagSink.start();

        gui::menu.registerEntry(name, menuHandler, this, this);
    }

    ~FLRCDecoderModule() {
        gui::menu.removeEntry(name);
        if (enabled) {
            dsp.stop();
            dataSink.stop();
            diagSink.stop();
            sigpath::vfoManager.deleteVFO(vfo);
        }
    }

    void postInit() {}

    void enable() {
        double bw = gui::waterfall.getBandwidth();
        vfo = sigpath::vfoManager.createVFO(name, ImGui::WaterfallVFO::REF_CENTER, std::clamp<double>(0, -bw / 2.0, bw / 2.0), 2000000, 8000000, 500000, 10000000, false);
        vfo->setSnapInterval(1000);

        dsp.setInput(vfo->output);
        dsp.start();
        dataSink.start();
        diagSink.start();

        enabled = true;
    }

    void disable() {
        dsp.stop();
        dataSink.stop();
        diagSink.stop();
        sigpath::vfoManager.deleteVFO(vfo);
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

private:
    static void _dspDataHandler(float* data, int count, void* ctx) {
        FLRCDecoderModule* _this = (FLRCDecoderModule*)ctx;
        auto frames = _this->detector.processSamples(data, count);
        if (!frames.empty()) {
            std::lock_guard<std::mutex> lck(_this->frameMtx);
            for (const auto& frame : frames) {
                // Forward to protocol handlers
                for (auto& handler : _this->protoHandlers) {
                    handler->processFrame(frame);
                }
                // Save to frame history
                _this->frameHistory.push_front(frame);
                if (_this->frameHistory.size() > 100) {
                    _this->frameHistory.pop_back();
                }
                _this->totalFrameCount++;
                if (frame.crcValid) _this->validFrameCount++;
            }
        }
    }

    static void _diagHandler(float* data, int count, void* ctx) {
        FLRCDecoderModule* _this = (FLRCDecoderModule*)ctx;
        float* buf = _this->diag.acquireBuffer();
        int copyCount = std::min(count, 1024);
        memcpy(buf, data, copyCount * sizeof(float));
        _this->diag.releaseBuffer();
    }

    static void menuHandler(void* ctx) {
        FLRCDecoderModule* _this = (FLRCDecoderModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (!_this->enabled) { style::beginDisabled(); }

        // Structured TabBar UI Layout
        if (ImGui::BeginTabBar(("flrc_tabs_" + _this->name).c_str())) {
            
            // Tab 1: Demodulation & Physical Layer
            if (ImGui::BeginTabItem(("Demod##" + _this->name).c_str())) {
                _this->renderDemodTab();
                ImGui::EndTabItem();
            }

            // Tab 2: Framing & Sync
            if (ImGui::BeginTabItem(("Framing##" + _this->name).c_str())) {
                _this->renderFramingTab();
                ImGui::EndTabItem();
            }

            // Tab 3: Protocol Inspector
            if (ImGui::BeginTabItem(("Protocol##" + _this->name).c_str())) {
                _this->renderProtocolTab();
                ImGui::EndTabItem();
            }

            // Tab 4: Frame Log & Stats
            if (ImGui::BeginTabItem(("Log & Stats##" + _this->name).c_str())) {
                _this->renderLogTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        if (!_this->enabled) { style::endDisabled(); }
    }

    void renderDemodTab() {
        ImGui::Spacing();
        ImGui::LeftLabel("Preset");
        ImGui::FillWidth();
        if (ImGui::Combo(("##preset_" + name).c_str(), &selectedPresetId, presets.txt)) {
            applyPreset(presets.value(selectedPresetId), demodCfg, framingCfg);
            dsp.setConfig(demodCfg);
            detector.setConfig(demodCfg, framingCfg);
        }

        ImGui::Separator();
        ImGui::Text("Physical Layer Parameters:");

        float symRateK = (float)(demodCfg.symbolRate / 1000.0);
        ImGui::LeftLabel("Bitrate (kbps)");
        ImGui::FillWidth();
        if (ImGui::InputFloat(("##symrate_" + name).c_str(), &symRateK, 10.0f, 100.0f, "%.1f")) {
            demodCfg.symbolRate = std::max(1000.0, symRateK * 1000.0);
            dsp.setConfig(demodCfg);
            detector.setConfig(demodCfg, framingCfg);
        }

        float devK = (float)(demodCfg.deviation / 1000.0);
        ImGui::LeftLabel("Deviation (kHz)");
        ImGui::FillWidth();
        if (ImGui::InputFloat(("##dev_" + name).c_str(), &devK, 5.0f, 25.0f, "%.1f")) {
            demodCfg.deviation = std::max(1000.0, devK * 1000.0);
            dsp.setConfig(demodCfg);
            detector.setConfig(demodCfg, framingCfg);
        }

        float cutoffK = (float)(demodCfg.filterCutoff / 1000.0);
        ImGui::LeftLabel("LPF Cutoff (kHz)");
        ImGui::FillWidth();
        if (ImGui::InputFloat(("##cutoff_" + name).c_str(), &cutoffK, 10.0f, 50.0f, "%.1f")) {
            demodCfg.filterCutoff = std::max(1000.0, cutoffK * 1000.0);
            dsp.setConfig(demodCfg);
            detector.setConfig(demodCfg, framingCfg);
        }

        ImGui::Spacing();
        ImGui::Text("Instantaneous Frequency / Eye Diagram:");
        ImGui::FillWidth();
        diag.draw();
    }

    void renderFramingTab() {
        ImGui::Spacing();
        ImGui::Text("Preamble & Sync Settings:");

        if (ImGui::Checkbox(("AGC Preamble (32-bit 0101..)##" + name).c_str(), &framingCfg.enableAgcPreamble)) {
            detector.setConfig(demodCfg, framingCfg);
        }

        float agcScore = demodCfg.agcScoreThreshold;
        ImGui::LeftLabel("AGC Threshold");
        ImGui::FillWidth();
        if (ImGui::SliderFloat(("##agcthresh_" + name).c_str(), &agcScore, 1.0f, 10.0f, "%.1f")) {
            demodCfg.agcScoreThreshold = agcScore;
            detector.setConfig(demodCfg, framingCfg);
        }

        if (ImGui::Checkbox(("Timing Preamble (21-bit)##" + name).c_str(), &framingCfg.enableTimingPreamble)) {
            detector.setConfig(demodCfg, framingCfg);
        }

        ImGui::LeftLabel("Timing Tolerance (bits)");
        ImGui::FillWidth();
        if (ImGui::SliderInt(("##timetol_" + name).c_str(), &framingCfg.timingToleranceBits, 0, 5)) {
            detector.setConfig(demodCfg, framingCfg);
        }

        ImGui::Separator();
        ImGui::Text("Sync Word & Masking:");

        char syncHexBuf[16];
        snprintf(syncHexBuf, sizeof(syncHexBuf), "%08X", framingCfg.syncWord);
        ImGui::LeftLabel("Sync Word (Hex)");
        ImGui::FillWidth();
        if (ImGui::InputText(("##syncword_" + name).c_str(), syncHexBuf, sizeof(syncHexBuf), ImGuiInputTextFlags_CharsHexadecimal)) {
            framingCfg.syncWord = (uint32_t)strtoul(syncHexBuf, nullptr, 16);
            detector.setConfig(demodCfg, framingCfg);
        }

        ImGui::LeftLabel("Mask Mode");
        ImGui::FillWidth();
        if (ImGui::Combo(("##maskmode_" + name).c_str(), &selectedMaskId, maskModes.txt)) {
            framingCfg.maskMode = maskModes.value(selectedMaskId);
            detector.setConfig(demodCfg, framingCfg);
        }

        if (ImGui::Checkbox(("Differential CumXOR Decode##" + name).c_str(), &framingCfg.differentialDecode)) {
            detector.setConfig(demodCfg, framingCfg);
        }

        if (ImGui::Checkbox(("Hardware CRC-32 Verification##" + name).c_str(), &framingCfg.enableHwCrc)) {
            detector.setConfig(demodCfg, framingCfg);
        }
    }

    void renderProtocolTab() {
        ImGui::Spacing();
        ImGui::LeftLabel("Protocol Parser");
        ImGui::FillWidth();
        ImGui::Combo(("##protocol_" + name).c_str(), &selectedProtoId, protocols.txt);

        ImGui::Separator();
        if (selectedProtoId >= 0 && selectedProtoId < (int)protoHandlers.size()) {
            protoHandlers[selectedProtoId]->renderUI();
        }
    }

    void renderLogTab() {
        ImGui::Spacing();
        ImGui::Text("Statistics: Total: %llu | CRC OK: %llu", (unsigned long long)totalFrameCount, (unsigned long long)validFrameCount);
        ImGui::SameLine();
        if (ImGui::Button(("Clear Log##" + name).c_str())) {
            std::lock_guard<std::mutex> lck(frameMtx);
            frameHistory.clear();
            totalFrameCount = 0;
            validFrameCount = 0;
            for (auto& h : protoHandlers) h->reset();
        }

        ImGui::Separator();
        ImGui::Text("Recent Frames (Last %zu):", frameHistory.size());

        std::lock_guard<std::mutex> lck(frameMtx);
        if (frameHistory.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No frames captured yet.");
            return;
        }

        ImGui::BeginChild(("frame_scroll_" + name).c_str(), ImVec2(0, 180), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (size_t i = 0; i < frameHistory.size(); i++) {
            const auto& f = frameHistory[i];
            ImVec4 color = f.crcValid ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(color, "[#%03zu] Sync: 0x%08X | Mask: 0x%02X | CRC: %s | Len: %zuB",
                               i + 1, f.syncWord, f.mask, f.crcValid ? "OK" : "FAIL", f.payload.size());

            // Hex preview
            std::string preview;
            for (size_t j = 0; j < std::min<size_t>(f.payload.size(), 16); j++) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X ", f.payload[j]);
                preview += buf;
            }
            if (f.payload.size() > 16) preview += "...";
            ImGui::Text("       %s", preview.c_str());
        }
        ImGui::EndChild();
    }

    std::string name;
    bool enabled = true;

    // Configs
    DemodConfig demodCfg;
    FramingConfig framingCfg;

    int selectedPresetId = 0;
    int selectedMaskId = 0;
    int selectedProtoId = 0;

    OptionList<int, PresetType> presets;
    OptionList<int, MaskMode> maskModes;
    OptionList<int, ProtocolType> protocols;

    // Protocol Handlers
    std::vector<std::unique_ptr<IProtocolHandler>> protoHandlers;

    // DSP & Pipeline
    VFOManager::VFO* vfo = nullptr;
    FLRCDSP dsp;
    FrameDetector detector;
    dsp::sink::Handler<float> dataSink;
    dsp::sink::Handler<float> diagSink;

    ImGui::SymbolDiagram diag;

    // Frame logging
    std::mutex frameMtx;
    std::deque<DecodedFrame> frameHistory;
    uint64_t totalFrameCount = 0;
    uint64_t validFrameCount = 0;
};

} // namespace flrc

MOD_EXPORT void _INIT_() {
    json def = json({});
    config.setPath(core::args["root"].s() + "/flrc_decoder_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new flrc::FLRCDecoderModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (flrc::FLRCDecoderModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
