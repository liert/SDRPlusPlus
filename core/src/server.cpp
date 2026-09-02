#include "server.h"
#include "core.h"
#include "web_server.h"
#include <utils/flog.h>
#include <version.h>
#include <config.h>
#include <filesystem>
#include <dsp/types.h>
#include <signal_path/signal_path.h>
#include <gui/smgui.h>
#include <utils/optionlist.h>
#include "dsp/compression/sample_stream_compressor.h"
#include "dsp/sink/handler_sink.h"
#include <zstd.h>

namespace server {
    dsp::stream<dsp::complex_t> dummyInput;
    dsp::compression::SampleStreamCompressor comp;
    dsp::sink::Handler<uint8_t> hnd;
    dsp::sink::Handler<dsp::complex_t> iqFftHandler;

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

    static void _fftSampleHandler(dsp::complex_t* data, int count, void* ctx) {
        web_server::processIqSamples(data, count, sampleRate);
    }

    int main() {
        flog::info("=====| SDR++ C++ HEADLESS SERVER ENGINE |=====");

        // Init DSP & Compression
        comp.init(&dummyInput, dsp::compression::PCM_TYPE_I8);
        hnd.init(&comp.out, _testServerHandler, NULL);
        iqFftHandler.init(&dummyInput, _fftSampleHandler, NULL);

        rbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        bbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];

        comp.start();
        hnd.start();
        iqFftHandler.start();

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

        // Initialize compressor
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
        else {
            flog::warn("Module directory {0} does not exist", modulesDir);
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

        // Do post-init
        core::moduleManager.doPostInitAll();

        // Generate source list
        auto list = sigpath::sourceManager.getSourceNames();
        for (auto& name : list) {
            sourceList.define(name, name);
        }

        if (sourceList.keyExists(sourceName)) {
            sourceId = sourceList.keyId(sourceName);
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
        } else if (!list.empty()) {
            sigpath::sourceManager.selectSource(list[0]);
        }

        std::string host = (std::string)core::args["addr"];
        int port = (int)core::args["port"];

        // Start High-Performance WebUI WebSocket & HTTP Server on the listening port
        web_server::setCommandHandler([](const std::string& cmd, const nlohmann::json& params) -> nlohmann::json {
            nlohmann::json res;
            if (cmd == "start") {
                sigpath::sourceManager.start();
                running = true;
                res["status"] = "ok";
                res["running"] = true;
            } else if (cmd == "stop") {
                sigpath::sourceManager.stop();
                running = false;
                res["status"] = "ok";
                res["running"] = false;
            } else if (cmd == "set_freq" && params.contains("freq")) {
                double freq = params["freq"];
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
                res["status"] = "ok";
            } else if (cmd == "set_source" && params.contains("source")) {
                std::string sname = params["source"];
                if (sourceList.keyExists(sname)) {
                    sourceId = sourceList.keyId(sname);
                    sigpath::sourceManager.selectSource(sname);
                    res["status"] = "ok";
                    res["source"] = sname;
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
            }
            return res;
        });

        web_server::start(port, host);

        // Also start legacy TCP client listener for backward compatibility
        try {
            listener = net::listen(host, port + 1);
            if (listener) {
                listener->acceptAsync(_clientHandler, NULL);
            }
        } catch (...) {}

        flog::info("🚀 SDR++ C++ Backend Engine is READY on {0}:{1}", host, port);
        while(1) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

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
        comp.setPCMType(dsp::compression::PCM_TYPE_I16);
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

    void _testServerHandler(uint8_t* data, int count, void* ctx) {
        if (compression) {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND_COMPRESSED;
            bb_pkt_hdr->size = sizeof(PacketHeader) + (uint32_t)ZSTD_compressCCtx(cctx, &bbuf[sizeof(PacketHeader)], SERVER_MAX_PACKET_SIZE-sizeof(PacketHeader), data, count, 1);
        }
        else {
            bb_pkt_hdr->type = PACKET_TYPE_BASEBAND;
            bb_pkt_hdr->size = sizeof(PacketHeader) + count;
            memcpy(&bbuf[sizeof(PacketHeader)], data, count);
        }

        if (client && client->isOpen()) { client->write(bb_pkt_hdr->size, bbuf); }
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        comp.setInput(stream);
        iqFftHandler.setInput(stream);
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
