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

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

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

    static void syncShmState() {
        bool fLoaded = false;
        std::string curFile = "";
        if (core::moduleManager.modules.find("file_source") != core::moduleManager.modules.end()) {
            typedef bool (*has_file_fn)();
            typedef const char* (*get_path_fn)();
            auto hf = (has_file_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_has_file");
            auto gp = (get_path_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_get_path");
            if (hf) fLoaded = hf();
            if (gp && gp()) curFile = gp();
        }
        shm_manager::updateState(running, sigpath::sourceManager.getSelectedSource(), centerFreq, sampleRate, lnaGain, vgaGain, ampEnable, biasTEnable, "", fLoaded, curFile);
    }

#ifdef _WIN32
    static bool isGuiRunning() {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (Process32First(hSnap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "SDRPlusPlus.exe") == 0) {
                    found = true;
                    break;
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return found;
    }

    static void launchGui() {
        if (isGuiRunning()) {
            flog::info("Frontend UI (SDRPlusPlus.exe) is already active.");
            return;
        }

        wchar_t exePathW[MAX_PATH];
        GetModuleFileNameW(NULL, exePathW, MAX_PATH);
        std::filesystem::path currentDir = std::filesystem::path(exePathW).parent_path();
        std::filesystem::path guiPath = currentDir / L"SDRPlusPlus.exe";

        std::error_code ec;
        if (std::filesystem::is_regular_file(guiPath, ec)) {
            flog::info("🚀 [Auto-Launcher] Launching Frontend UI: {0}", guiPath.u8string());
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            std::wstring cmd = L"\"" + guiPath.wstring() + L"\"";
            if (CreateProcessW(NULL, (wchar_t*)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, currentDir.wstring().c_str(), &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                flog::error("Failed to launch Frontend UI, GetLastError={0}", (int)GetLastError());
            }
        } else {
            flog::warn("Frontend UI (SDRPlusPlus.exe) not found in directory: {0}", currentDir.u8string());
        }
    }
#endif

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
        if (!sourceName.empty() && sourceList.keyExists(sourceName)) {
            sourceId = sourceList.keyId(sourceName);
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
            selected = true;
        }
        if (!selected) {
            for (auto& name : list) {
                if (name == "HackRF" || name.find("HackRF") != std::string::npos || name.find("hackrf") != std::string::npos) {
                    sourceId = sourceList.keyId(name);
                    sigpath::sourceManager.selectSource(name);
                    selected = true;
                    break;
                }
            }
        }
        if (!selected && !list.empty()) {
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
                if (params.contains("source")) {
                    std::string reqSrc = params["source"];
                    std::string curSrc = sigpath::sourceManager.getSelectedSource();
                    if (!reqSrc.empty() && curSrc != reqSrc && curSrc.find(reqSrc) == std::string::npos) {
                        for (auto& name : sigpath::sourceManager.getSourceNames()) {
                            if (name == reqSrc || name.find(reqSrc) != std::string::npos) {
                                sigpath::sourceManager.selectSource(name);
                                break;
                            }
                        }
                    }
                } else if (sigpath::sourceManager.getSelectedSource().empty()) {
                    for (auto& name : sigpath::sourceManager.getSourceNames()) {
                        if (name.find("HackRF") != std::string::npos || name.find("hackrf") != std::string::npos) {
                            sigpath::sourceManager.selectSource(name);
                            break;
                        }
                    }
                }

                std::string sel = sigpath::sourceManager.getSelectedSource();
                if (sel == "File Source" || sel == "File") {
                    typedef bool (*has_file_fn)();
                    if (core::moduleManager.modules.find("file_source") != core::moduleManager.modules.end()) {
                        auto fn = (has_file_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_has_file");
                        if (fn && !fn()) {
                            flog::error("❌ Cannot start streaming: File Source has no valid IQ file loaded!");
                            res["status"] = "error";
                            res["message"] = "未选择有效 IQ 录制文件，请先在射频输入源中选择文件！";
                            res["running"] = false;
                            return res;
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
                        typedef bool (*file_set_path_fn)(const char*, bool);
                        auto fn = (file_set_path_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_set_path");
                        if (fn) {
                            std::string p = params["path"].get<std::string>();
                            bool ok = fn(p.c_str(), true);
                            flog::info("📂 [File Source] set_path '{0}' (success: {1})", p, ok);
                        }
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
            } else if (cmd == "set_device" && params.contains("serial")) {
                std::string serial = params["serial"];
                if (core::moduleManager.modules.find("hackrf_source") != core::moduleManager.modules.end()) {
                    typedef void (*hackrf_select_serial_fn)(const char*);
                    auto fn = (hackrf_select_serial_fn)GetProcAddress((HMODULE)core::moduleManager.modules["hackrf_source"].handle, "hackrf_select_serial");
                    if (fn) fn(serial.c_str());
                }
                res["status"] = "ok";
                res["device"] = serial;
            } else if (cmd == "set_file_path" && params.contains("path")) {
                std::string p = params["path"].get<std::string>();
                bool loop = params.value("loop", true);
                if (core::moduleManager.modules.find("file_source") != core::moduleManager.modules.end()) {
                    typedef bool (*file_set_path_fn)(const char*, bool);
                    auto fn = (file_set_path_fn)GetProcAddress((HMODULE)core::moduleManager.modules["file_source"].handle, "file_source_set_path");
                    if (fn) {
                        bool ok = fn(p.c_str(), loop);
                        flog::info("📂 [File Source] direct set_file_path '{0}' (success: {1})", p, ok);
                        res["status"] = ok ? "ok" : "error";
                        res["file_loaded"] = ok;
                    }
                }
            } else if (cmd == "set_fft" || cmd == "set_fft_params") {
                int fftSize = params.value("fftSize", 1024);
                int fftWindow = params.value("fftWindow", 0);
                int fftRate = params.value("fftRate", 60);
                shm_manager::setFftParams(fftSize, fftWindow, fftRate);
                res["status"] = "ok";
                res["fftSize"] = fftSize;
                res["fftWindow"] = fftWindow;
                res["fftRate"] = fftRate;
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

            syncShmState();
            return res;
        };

        // Initialize Native Windows Shared Memory Engine
        shm_manager::init();
        shm_manager::setCommandHandler(handleCmd);
        syncShmState();

        flog::info("🚀 SDR++ C++ Native Core Engine is READY (Pure Windows Shared Memory IPC)");

#ifdef _WIN32
        // Auto-launch Frontend UI if started standalone (not via GUI companion)
        bool fromGui = core::args["from-gui"].b();
        if (!fromGui) {
            launchGui();
        }
#endif

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
