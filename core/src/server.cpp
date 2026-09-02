#include "server.h"
#include "core.h"
#include "shm_manager.h"
#include <utils/flog.h>
#include <version.h>
#include <config.h>
#include <filesystem>
#include <dsp/types.h>
#include <signal_path/signal_path.h>
#include <gui/smgui.h>
#include <utils/optionlist.h>
#include "dsp/sink/handler_sink.h"

#if __has_include(<libhackrf/hackrf.h>)
#include <libhackrf/hackrf.h>
#elif __has_include(<hackrf.h>)
#include <hackrf.h>
#endif

namespace server {
    dsp::stream<dsp::complex_t> dummyInput;
    dsp::sink::Handler<dsp::complex_t> sampleHandler;

    OptionList<std::string, std::string> sourceList;
    int sourceId = 0;
    bool running = false;
    double sampleRate = 8000000.0;
    double centerFreq = 2400000000.0;
    int lnaGain = 32;
    int vgaGain = 20;
    bool ampEnable = false;
    bool biasTEnable = false;

    // Single-reader DSP sample handler: Zero-Deadlock, Ultra-Fast Shared Memory Pipeline
    static void _mainSampleHandler(dsp::complex_t* data, int count, void* ctx) {
        if (!data || count <= 0) return;

        // Process and write real-time FFT spectrum directly into Windows Shared Memory
        shm_manager::processIqSamples(data, count, sampleRate);
    }

    static nlohmann::json queryHackRfDevices() {
        nlohmann::json devs = nlohmann::json::array();
#if defined(LIBHACKRF_HACKRF_H) || defined(__HACKRF_H__) || defined(HACKRF_SUCCESS)
        try {
            hackrf_device_list_t* list = hackrf_device_list();
            if (list) {
                for (int i = 0; i < list->devicecount; i++) {
                    if (list->serial_numbers[i]) {
                        nlohmann::json d;
                        d["serial"] = list->serial_numbers[i];
                        d["name"] = std::string("HackRF One (") + (list->serial_numbers[i] + 16) + ")";
                        d["index"] = i;
                        devs.push_back(d);
                    }
                }
                hackrf_device_list_free(list);
            }
        } catch (...) {}
#endif
        return devs;
    }

    int main() {
        flog::info("=====| SDR++ C++ NATIVE SHARED MEMORY CORE ENGINE |=====");

        // Init DSP Pipeline with Single Reader (zero deadlock)
        sampleHandler.init(&dummyInput, _mainSampleHandler, NULL);
        sampleHandler.start();

        // Load config
        core::configManager.acquire();
        std::string modulesDir = core::configManager.conf["modulesDirectory"];
        std::vector<std::string> modules = core::configManager.conf["modules"];
        auto modList = core::configManager.conf["moduleInstances"].items();
        std::string sourceName = core::configManager.conf["source"];
        core::configManager.release();

        // Initialize SmGui in server mode
        SmGui::init(true);

        flog::info("Loading SDR modules...");
        if (std::filesystem::is_directory(std::filesystem::u8path(modulesDir))) {
            for (const auto& file : std::filesystem::directory_iterator(std::filesystem::u8path(modulesDir))) {
                std::string path = file.path().generic_u8string();
                std::string fn = file.path().filename().generic_u8string();
                if (file.path().extension().generic_u8string() != SDRPP_MOD_EXTENTSION) {
                    continue;
                }
                if (!file.is_regular_file()) { continue; }
                if (fn.find("source") == std::string::npos) { continue; }

                flog::info("Loading {0}", path);
                core::moduleManager.loadModule(path);
            }
        }

        for (auto const& apath : modules) {
            std::filesystem::path file = std::filesystem::u8path(apath);
            std::string path = file.generic_u8string();
            std::string fn = file.filename().generic_u8string();
            if (file.extension().generic_u8string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!std::filesystem::is_regular_file(file)) { continue; }
            if (fn.find("source") == std::string::npos) { continue; }

            flog::info("Loading {0}", path);
            core::moduleManager.loadModule(path);
        }

        // Create module instances
        for (auto const& [name, _module] : modList) {
            std::string mod = _module["module"];
            bool enabled = _module["enabled"];
            if (core::moduleManager.modules.find(mod) == core::moduleManager.modules.end()) { continue; }
            flog::info("Initializing {0} ({1})", name, mod);
            core::moduleManager.createInstance(name, mod);
            if (!enabled) { core::moduleManager.disableInstance(name); }
        }

        core::moduleManager.doPostInitAll();

        // Generate source list
        auto list = sigpath::sourceManager.getSourceNames();
        for (auto& name : list) {
            sourceList.define(name, name);
        }

        bool selected = false;
        for (auto& name : list) {
            if (name == "HackRF" || name.find("HackRF") != std::string::npos || name.find("hackrf") != std::string::npos) {
                sourceId = sourceList.keyId(name);
                sigpath::sourceManager.selectSource(name);
                selected = true;
                break;
            }
        }
        if (!selected && sourceList.keyExists(sourceName)) {
            sourceId = sourceList.keyId(sourceName);
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
        } else if (!selected && !list.empty()) {
            sigpath::sourceManager.selectSource(list[0]);
        }

        auto handleCmd = [](const std::string& cmd, const nlohmann::json& params) -> nlohmann::json {
            nlohmann::json res;
            if (cmd == "start") {
                if (running) {
                    res["status"] = "ok";
                    res["running"] = true;
                    res["source"] = sigpath::sourceManager.getSelectedSource();
                    return res;
                }
                if (sigpath::sourceManager.getSelectedSource().empty()) {
                    for (auto& name : sigpath::sourceManager.getSourceNames()) {
                        if (name.find("HackRF") != std::string::npos || name.find("hackrf") != std::string::npos) {
                            sigpath::sourceManager.selectSource(name);
                            break;
                        }
                    }
                }
                sigpath::sourceManager.start();
                typedef void (*hackrf_gain_fn)(float, float, bool, bool);
                if (core::moduleManager.modules.find("hackrf_source") != core::moduleManager.modules.end()) {
                    auto fn = (hackrf_gain_fn)GetProcAddress((HMODULE)core::moduleManager.modules["hackrf_source"].handle, "hackrf_apply_gain");
                    if (fn) fn((float)lnaGain, (float)vgaGain, ampEnable, biasTEnable);
                }
                running = true;
                res["status"] = "ok";
                res["running"] = true;
                res["source"] = sigpath::sourceManager.getSelectedSource();
            } else if (cmd == "stop") {
                if (!running) {
                    res["status"] = "ok";
                    res["running"] = false;
                    return res;
                }
                sigpath::sourceManager.stop();
                running = false;
                res["status"] = "ok";
                res["running"] = false;
            } else if (cmd == "set_freq" && params.contains("freq")) {
                double freq = params["freq"];
                centerFreq = freq;
                sigpath::sourceManager.tune(freq);
                flog::info("⚡ [Frequency] Tuned to {0:.6f} MHz ({1} Hz)", freq / 1e6, (uint64_t)freq);
                res["status"] = "ok";
                res["freq"] = freq;
            } else if (cmd == "set_samplerate" && params.contains("sampleRate")) {
                double sr = params["sampleRate"];
                sampleRate = sr;
                core::setInputSampleRate(sr);

                // 1. Update HackRF source module
                typedef void (*hackrf_sr_fn)(int);
                if (core::moduleManager.modules.find("hackrf_source") != core::moduleManager.modules.end()) {
                    auto fn = (hackrf_sr_fn)GetProcAddress((HMODULE)core::moduleManager.modules["hackrf_source"].handle, "hackrf_set_samplerate");
                    if (fn) fn((int)sr);
                }

                // 2. Update File source module
                typedef void (*file_sr_fn)(double);
                if (core::moduleManager.modules.find("file_source") != core::moduleManager.modules.end()) {
                    auto fn = (file_sr_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_set_samplerate");
                    if (fn) fn(sr);
                }

                flog::info("⚡ [SampleRate] Changed to {0:.3f} MSPS ({1} Hz) across C++ backend and hardware source", sr / 1e6, (uint64_t)sr);
                res["status"] = "ok";
                res["sampleRate"] = sr;
            } else if (cmd == "set_gain") {
                if (params.contains("lna")) lnaGain = params["lna"];
                if (params.contains("vga")) vgaGain = params["vga"];
                if (params.contains("amp")) ampEnable = params["amp"];
                if (params.contains("biasT")) biasTEnable = params["biasT"];

                typedef void (*hackrf_gain_fn)(float, float, bool, bool);
                if (core::moduleManager.modules.find("hackrf_source") != core::moduleManager.modules.end()) {
                    auto fn = (hackrf_gain_fn)GetProcAddress((HMODULE)core::moduleManager.modules["hackrf_source"].handle, "hackrf_apply_gain");
                    if (fn) fn((float)lnaGain, (float)vgaGain, ampEnable, biasTEnable);
                }

                flog::info("⚡ [Gain] Applied LNA={0}dB, VGA={1}dB, Amp={2}, BiasT={3}", lnaGain, vgaGain, ampEnable, biasTEnable);
                res["status"] = "ok";
                res["lna"] = lnaGain;
                res["vga"] = vgaGain;
                res["amp"] = ampEnable;
                res["biasT"] = biasTEnable;
            } else if (cmd == "set_source" && params.contains("source")) {
                std::string sname = params["source"];
                std::string matched = "";
                for (auto& name : sigpath::sourceManager.getSourceNames()) {
                    if (name == sname || name.find(sname) != std::string::npos || sname.find(name) != std::string::npos) {
                        matched = name;
                        break;
                    }
                }
                if (!matched.empty()) {
                    bool wasRunning = running;
                    if (wasRunning) {
                        sigpath::sourceManager.stop();
                        running = false;
                    }
                    sourceId = sourceList.keyId(matched);
                    sigpath::sourceManager.selectSource(matched);

                    if (params.contains("path") && core::moduleManager.modules.find("file_source") != core::moduleManager.modules.end()) {
                        typedef void (*file_set_path_fn)(const char*, bool);
                        auto fn = (file_set_path_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_set_path");
                        if (fn) fn(params["path"].get<std::string>().c_str(), true);
                    }

                    if (wasRunning) {
                        sigpath::sourceManager.start();
                        running = true;
                    }
                    res["status"] = "ok";
                    res["source"] = matched;
                } else {
                    res["status"] = "error";
                    res["message"] = "Source not found";
                }
            } else if (cmd == "get_sources") {
                nlohmann::json slist = nlohmann::json::array();
                for (auto& name : sigpath::sourceManager.getSourceNames()) {
                    slist.push_back(name);
                }
                res["sources"] = slist;
                res["status"] = "ok";
            } else if (cmd == "get_devices" || cmd == "get_hackrf_devices") {
                res["type"] = "devices";
                res["devices"] = queryHackRfDevices();
                res["status"] = "ok";
            } else if (cmd == "get_status") {
                res["type"] = "status";
                res["backend"] = "SDRPlusPlus C++ Core Engine";
                res["version"] = "1.3.0";
                res["running"] = running;
                res["source"] = sigpath::sourceManager.getSelectedSource();
                res["sampleRate"] = sampleRate;
                res["centerFreq"] = centerFreq;
                res["lna"] = lnaGain;
                res["vga"] = vgaGain;
                res["amp"] = ampEnable;
                res["biasT"] = biasTEnable;
                res["devices"] = queryHackRfDevices();
                res["status"] = "ok";
            }

            shm_manager::updateState(running, sigpath::sourceManager.getSelectedSource(), centerFreq, sampleRate, lnaGain, vgaGain, ampEnable, biasTEnable, "");
            return res;
        };

        // Initialize Native Windows Shared Memory Engine
        shm_manager::init();
        shm_manager::setCommandHandler(handleCmd);
        shm_manager::updateState(running, sigpath::sourceManager.getSelectedSource(), centerFreq, sampleRate, lnaGain, vgaGain, ampEnable, biasTEnable, "");

        flog::info("🚀 SDR++ C++ Native Core Engine is READY (Pure Windows Shared Memory IPC)");
        while(1) {
            shm_manager::checkCommands();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return 0;
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        sampleHandler.setInput(stream);
    }

    void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue) {}
    void sendUI(Command originCmd, std::string diffId, SmGui::DrawListElem diffValue) {}
    void sendError(Error err) {}
    void sendSampleRate(double sr) { sampleRate = sr; }
    void setInputSampleRate(double sr) { sampleRate = sr; }
    void sendPacket(PacketType type, int len) {}
    void sendCommand(Command cmd, int len) {}
    void sendCommandAck(Command cmd, int len) {}
}
