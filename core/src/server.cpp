#include "server.h"
#include "core.h"
#include "web_server.h"
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
#include <zstd.h>

#if __has_include(<libhackrf/hackrf.h>)
#include <libhackrf/hackrf.h>
#elif __has_include(<hackrf.h>)
#include <hackrf.h>
#endif

namespace server {
    dsp::stream<dsp::complex_t> dummyInput;
    dsp::sink::Handler<dsp::complex_t> sampleHandler;

    net::Conn client;
    uint8_t* rbuf = NULL;
    uint8_t* sbuf = NULL;
    uint8_t* bbuf = NULL;

    PacketHeader* r_pkt_hdr = NULL;
    uint8_t* r_pkt_data = NULL;
    CommandHeader* r_cmd_hdr = NULL;
    uint8_t* r_cmd_data = NULL;

    PacketHeader* s_pkt_hdr = NULL;
    uint8_t* s_pkt_data = NULL;
    CommandHeader* s_cmd_hdr = NULL;
    uint8_t* s_cmd_data = NULL;

    PacketHeader* bb_pkt_hdr = NULL;
    uint8_t* bb_pkt_data = NULL;

    SmGui::DrawListElem dummyElem;

    ZSTD_CCtx* cctx;

    net::Listener listener;

    OptionList<std::string, std::string> sourceList;
    int sourceId = 0;
    bool running = false;
    bool compression = false;
    double sampleRate = 8000000.0;
    double centerFreq = 2400000000.0;
    int lnaGain = 32;
    int vgaGain = 20;
    bool ampEnable = false;
    bool biasTEnable = false;

    // Single-reader DSP sample handler: Zero-Deadlock, Full-Throughput Stream Pipeline
    static void _mainSampleHandler(dsp::complex_t* data, int count, void* ctx) {
        if (!data || count <= 0) return;

        // 1. Process and broadcast real-time FFT spectrum to WebUI WebSocket clients
        web_server::processIqSamples(data, count, sampleRate);

        // 2. Stream to legacy TCP client if connected
        if (client && client->isOpen()) {
            if (compression) {
                bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_COMPRESSED;
                bb_pkt_hdr->size = sizeof(PacketHeader) + (uint32_t)ZSTD_compressCCtx(cctx, &bbuf[sizeof(PacketHeader)], SERVER_MAX_PACKET_SIZE-sizeof(PacketHeader), data, count * sizeof(dsp::complex_t), 1);
            }
            else {
                bb_pkt_hdr->type = PACKET_TYPE_BASEBAND;
                bb_pkt_hdr->size = sizeof(PacketHeader) + count * sizeof(dsp::complex_t);
                memcpy(&bbuf[sizeof(PacketHeader)], data, count * sizeof(dsp::complex_t));
            }
            client->write(bb_pkt_hdr->size, bbuf);
        }
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
        flog::info("=====| SDR++ C++ HEADLESS SERVER ENGINE |=====");

        // Init DSP Pipeline with Single Reader (zero deadlock)
        sampleHandler.init(&dummyInput, _mainSampleHandler, NULL);
        sampleHandler.start();

        rbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        bbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuf;
        r_pkt_data = &rbuf[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuf;
        s_pkt_data = &sbuf[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        bb_pkt_hdr = (PacketHeader*)bbuf;
        bb_pkt_data = &bbuf[sizeof(PacketHeader)];

        cctx = ZSTD_createCCtx();

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

        std::string host = (std::string)core::args["addr"];
        int port = (int)core::args["port"];

        auto handleCmd = [](const std::string& cmd, const nlohmann::json& params) -> nlohmann::json {
            nlohmann::json res;
            if (cmd == "start") {
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
                sigpath::sourceManager.stop();
                running = false;
                res["status"] = "ok";
                res["running"] = false;
            } else if (cmd == "set_freq" && params.contains("freq")) {
                double freq = params["freq"];
                centerFreq = freq;
                sigpath::sourceManager.tune(freq);
                res["status"] = "ok";
                res["freq"] = freq;
            } else if (cmd == "set_samplerate" && params.contains("sampleRate")) {
                double sr = params["sampleRate"];
                sampleRate = sr;
                core::setInputSampleRate(sr);
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
                    sourceId = sourceList.keyId(matched);
                    sigpath::sourceManager.selectSource(matched);
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

        web_server::setCommandHandler(handleCmd);
        web_server::start(port, host);

        // Start legacy TCP listener
        try {
            listener = net::listen(host, port + 1);
            if (listener) {
                listener->acceptAsync(_clientHandler, NULL);
            }
        } catch (...) {}

        flog::info("🚀 SDR++ C++ Backend Engine is READY (SHM + WS {0}:{1})", host, port);
        while(1) {
            shm_manager::checkCommands();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        return 0;
    }

    void _clientHandler(net::Conn conn, void* ctx) {
        if (client && client->isOpen()) {
            conn->close();
            if (listener) listener->acceptAsync(_clientHandler, NULL);
            return;
        }

        client = std::move(conn);
        client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, NULL);

        sigpath::sourceManager.stop();
        compression = false;

        sendSampleRate(sampleRate);
        if (listener) listener->acceptAsync(_clientHandler, NULL);
    }

    void _packetHandler(int count, uint8_t* buf, void* ctx) {
        PacketHeader* hdr = (PacketHeader*)buf;
        int len = 0;
        int read = 0;
        int goal = hdr->size - sizeof(PacketHeader);
        while (len < goal) {
            read = client->read(goal - len, &buf[sizeof(PacketHeader) + len]);
            if (read < 0) { return; };
            len += read;
        }

        if (hdr->type == PACKET_TYPE_COMMAND && hdr->size >= sizeof(PacketHeader) + sizeof(CommandHeader)) {
            CommandHeader* chdr = (CommandHeader*)&buf[sizeof(PacketHeader)];
            commandHandler((Command)chdr->cmd, &buf[sizeof(PacketHeader) + sizeof(CommandHeader)], hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
        }
        else {
            sendError(ERROR_INVALID_PACKET);
        }

        client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, NULL);
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        sampleHandler.setInput(stream);
    }

    void commandHandler(Command cmd, uint8_t* data, int len) {
        if (cmd == COMMAND_START) {
            sigpath::sourceManager.start();
            running = true;
        }
        else if (cmd == COMMAND_STOP) {
            sigpath::sourceManager.stop();
            running = false;
        }
        else if (cmd == COMMAND_SET_FREQUENCY && len == 8) {
            sigpath::sourceManager.tune(*(double*)data);
            sendCommandAck(COMMAND_SET_FREQUENCY, 0);
        }
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
