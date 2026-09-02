#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#include "web_server.h"
#include <utils/flog.h>
#include <signal_path/signal_path.h>
#include <fftw3.h>
#include <volk/volk.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace web_server {

    // === SHA-1 & Base64 Implementation for RFC 6455 Handshake ===
    namespace crypto {
        static inline uint32_t rol(uint32_t value, size_t bits) {
            return (value << bits) | (value >> (32 - bits));
        }

        static inline uint32_t blk(const uint32_t block[16], size_t i) {
            return rol(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^ block[(i + 2) & 15] ^ block[i], 1);
        }

        static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
            uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
            uint32_t block[16];
            for (size_t i = 0; i < 16; i++) {
                block[i] = ((uint32_t)buffer[4 * i] << 24) |
                           ((uint32_t)buffer[4 * i + 1] << 16) |
                           ((uint32_t)buffer[4 * i + 2] << 8) |
                           ((uint32_t)buffer[4 * i + 3]);
            }

            for (size_t i = 0; i < 16; i++) {
                uint32_t t = rol(a, 5) + ((b & c) | (~b & d)) + e + block[i] + 0x5a827999;
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            for (size_t i = 16; i < 20; i++) {
                block[i & 15] = blk(block, i & 15);
                uint32_t t = rol(a, 5) + ((b & c) | (~b & d)) + e + block[i & 15] + 0x5a827999;
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            for (size_t i = 20; i < 40; i++) {
                block[i & 15] = blk(block, i & 15);
                uint32_t t = rol(a, 5) + (b ^ c ^ d) + e + block[i & 15] + 0x6ed9eba1;
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            for (size_t i = 40; i < 60; i++) {
                block[i & 15] = blk(block, i & 15);
                uint32_t t = rol(a, 5) + ((b & c) | (b & d) | (c & d)) + e + block[i & 15] + 0x8f1bbcdc;
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }
            for (size_t i = 60; i < 80; i++) {
                block[i & 15] = blk(block, i & 15);
                uint32_t t = rol(a, 5) + (b ^ c ^ d) + e + block[i & 15] + 0xca62c1d6;
                e = d; d = c; c = rol(b, 30); b = a; a = t;
            }

            state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
        }

        static std::string sha1_raw(const std::string& input) {
            uint32_t state[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
            uint64_t count = 0;
            uint8_t buffer[64];
            size_t offset = 0;

            for (char ch : input) {
                buffer[offset++] = (uint8_t)ch;
                if (offset == 64) {
                    sha1_transform(state, buffer);
                    count += 512;
                    offset = 0;
                }
            }

            count += offset * 8;
            buffer[offset++] = 0x80;
            if (offset > 56) {
                while (offset < 64) buffer[offset++] = 0;
                sha1_transform(state, buffer);
                offset = 0;
            }
            while (offset < 56) buffer[offset++] = 0;
            for (int i = 7; i >= 0; i--) {
                buffer[56 + i] = (uint8_t)(count >> ((7 - i) * 8));
            }
            sha1_transform(state, buffer);

            std::string digest;
            digest.resize(20);
            for (size_t i = 0; i < 5; i++) {
                digest[i * 4 + 0] = (uint8_t)(state[i] >> 24);
                digest[i * 4 + 1] = (uint8_t)(state[i] >> 16);
                digest[i * 4 + 2] = (uint8_t)(state[i] >> 8);
                digest[i * 4 + 3] = (uint8_t)(state[i]);
            }
            return digest;
        }

        static std::string base64_encode(const std::string& in) {
            static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            int val = 0, valb = -6;
            for (uint8_t c : in) {
                val = (val << 8) + c;
                valb += 8;
                while (valb >= 0) {
                    out.push_back(b64[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
            while (out.size() % 4) out.push_back('=');
            return out;
        }

        static std::string make_websocket_accept(const std::string& key) {
            std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            return base64_encode(sha1_raw(magic));
        }
    }

    // === Server State & Client Connections ===
    struct WsClient {
        SOCKET sock;
        bool isWebSocket;
        std::string rxBuffer;
    };

    static std::atomic<bool> serverRunning(false);
    static SOCKET listenSocket = INVALID_SOCKET;
    static std::thread serverThread;
    static std::vector<std::shared_ptr<WsClient>> clients;
    static std::mutex clientsMutex;
    static CommandHandler customCmdHandler = nullptr;

    // === FFT Processing State ===
    static const int FFT_SIZE = 1024;
    static fftwf_complex* fftwIn = nullptr;
    static fftwf_complex* fftwOut = nullptr;
    static fftwf_plan fftPlan = nullptr;
    static float windowLut[FFT_SIZE];
    static float fftOutputDb[FFT_SIZE];
    static std::mutex fftMutex;
    static std::chrono::steady_clock::time_point lastFftTime;

    static void initFft() {
        fftwIn = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        fftwOut = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
        fftPlan = fftwf_plan_dft_1d(FFT_SIZE, fftwIn, fftwOut, FFTW_FORWARD, FFTW_ESTIMATE);

        // Precompute Blackman-Harris 4-term window
        for (int i = 0; i < FFT_SIZE; i++) {
            double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
            double f = (2.0 * M_PI * i) / (FFT_SIZE - 1);
            windowLut[i] = (float)(a0 - a1 * cos(f) + a2 * cos(2.0 * f) - a3 * cos(3.0 * f));
        }
        lastFftTime = std::chrono::steady_clock::now();
    }

    static void cleanupFft() {
        if (fftPlan) { fftwf_destroy_plan(fftPlan); fftPlan = nullptr; }
        if (fftwIn) { fftwf_free(fftwIn); fftwIn = nullptr; }
        if (fftwOut) { fftwf_free(fftwOut); fftwOut = nullptr; }
    }

    // === WebSocket Frame Transmission Helpers ===
    static void sendWsFrame(SOCKET sock, uint8_t opcode, const uint8_t* payload, size_t len) {
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | opcode); // FIN + Opcode

        if (len <= 125) {
            frame.push_back((uint8_t)len);
        } else if (len <= 65535) {
            frame.push_back(126);
            frame.push_back((uint8_t)((len >> 8) & 0xFF));
            frame.push_back((uint8_t)(len & 0xFF));
        } else {
            frame.push_back(127);
            for (int i = 7; i >= 0; i--) {
                frame.push_back((uint8_t)((len >> (i * 8)) & 0xFF));
            }
        }

        frame.insert(frame.end(), payload, payload + len);
        send(sock, (const char*)frame.data(), (int)frame.size(), 0);
    }

    void broadcastFft(const float* fftDb, int size) {
        if (!fftDb || size <= 0) return;
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto it = clients.begin(); it != clients.end();) {
            auto client = *it;
            if (client->isWebSocket) {
                sendWsFrame(client->sock, 0x02, (const uint8_t*)fftDb, size * sizeof(float));
                ++it;
            } else {
                ++it;
            }
        }
    }

    void broadcastPacket(const nlohmann::json& packet) {
        std::string jsonStr = packet.dump();
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto it = clients.begin(); it != clients.end();) {
            auto client = *it;
            if (client->isWebSocket) {
                sendWsFrame(client->sock, 0x01, (const uint8_t*)jsonStr.data(), jsonStr.size());
                ++it;
            } else {
                ++it;
            }
        }
    }

    void processIqSamples(const dsp::complex_t* samples, int count, double sampleRate) {
        if (!samples || count < FFT_SIZE || !fftPlan) return;

        // Rate limit FFT broadcast to ~45 FPS to maintain super smooth UI
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFftTime).count();
        if (elapsedMs < 22) return;
        lastFftTime = now;

        std::lock_guard<std::mutex> lock(fftMutex);

        // Apply window and copy into FFTW input
        for (int i = 0; i < FFT_SIZE; i++) {
            fftwIn[i][0] = samples[i].re * windowLut[i];
            fftwIn[i][1] = samples[i].im * windowLut[i];
        }

        fftwf_execute(fftPlan);

        // Compute power in dBm and FFT shift (center DC bin at index 512)
        int halfFft = FFT_SIZE / 2;
        for (int i = 0; i < FFT_SIZE; i++) {
            int srcIdx = (i + halfFft) % FFT_SIZE;
            float re = fftwOut[srcIdx][0];
            float im = fftwOut[srcIdx][1];
            float pwr = re * re + im * im + 1e-12f;
            float dbm = 10.0f * log10f(pwr) - 20.0f;
            fftOutputDb[i] = std::max(-120.0f, std::min(10.0f, dbm));
        }

        // Broadcast binary Float32Array over WebSocket
        broadcastFft(fftOutputDb, FFT_SIZE);
    }

    nlohmann::json getStatusJson() {
        nlohmann::json st;
        st["backend"] = "SDRPlusPlus C++ Core Engine";
        st["version"] = "1.3.0";
        st["running"] = sigpath::sourceManager.getSelectedSource() != "";
        st["source"] = sigpath::sourceManager.getSelectedSource();
        st["sampleRate"] = 8000000.0;
        st["centerFreq"] = 2400000000.0;
        st["activeClients"] = clients.size();
        return st;
    }

    void setCommandHandler(CommandHandler handler) {
        customCmdHandler = handler;
    }

    // === Client Request / WebSocket Dispatcher ===
    static void handleIncomingData(std::shared_ptr<WsClient> client, const char* data, int len) {
        client->rxBuffer.append(data, len);

        if (!client->isWebSocket) {
            // Check for HTTP Request End
            size_t headerEnd = client->rxBuffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) return;

            std::string req = client->rxBuffer.substr(0, headerEnd);
            client->rxBuffer.erase(0, headerEnd + 4);

            // 1. WebSocket Upgrade Handshake
            if (req.find("Upgrade: websocket") != std::string::npos || req.find("upgrade: websocket") != std::string::npos) {
                size_t keyPos = req.find("Sec-WebSocket-Key: ");
                if (keyPos == std::string::npos) keyPos = req.find("sec-websocket-key: ");

                if (keyPos != std::string::npos) {
                    size_t valStart = keyPos + 19;
                    size_t valEnd = req.find("\r\n", valStart);
                    std::string clientKey = req.substr(valStart, valEnd - valStart);
                    std::string acceptKey = crypto::make_websocket_accept(clientKey);

                    std::ostringstream response;
                    response << "HTTP/1.1 101 Switching Protocols\r\n"
                             << "Upgrade: websocket\r\n"
                             << "Connection: Upgrade\r\n"
                             << "Sec-WebSocket-Accept: " << acceptKey << "\r\n\r\n";

                    std::string respStr = response.str();
                    send(client->sock, respStr.c_str(), (int)respStr.size(), 0);
                    client->isWebSocket = true;
                    flog::info("WebUI WebSocket Client Connected successfully.");

                    // Send initial status JSON frame
                    nlohmann::json initMsg = getStatusJson();
                    initMsg["type"] = "status";
                    std::string initStr = initMsg.dump();
                    sendWsFrame(client->sock, 0x01, (const uint8_t*)initStr.data(), initStr.size());
                    return;
                }
            }

            // 2. HTTP REST Endpoints
            std::ostringstream httpResp;
            if (req.rfind("OPTIONS", 0) == 0) {
                httpResp << "HTTP/1.1 200 OK\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                         << "Access-Control-Allow-Headers: Content-Type\r\n\r\n";
            } else if (req.find("GET /api/status") != std::string::npos) {
                nlohmann::json st;
                if (customCmdHandler) st = customCmdHandler("get_status", nlohmann::json::object());
                else st = getStatusJson();
                std::string body = st.dump();
                httpResp << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: application/json\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Content-Length: " << body.size() << "\r\n\r\n"
                         << body;
            } else if (req.find("GET /api/devices") != std::string::npos || req.find("GET /api/hackrf/devices") != std::string::npos) {
                nlohmann::json devs;
                if (customCmdHandler) devs = customCmdHandler("get_devices", nlohmann::json::object());
                else devs = nlohmann::json::object();
                std::string body = devs.dump();
                httpResp << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: application/json\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Content-Length: " << body.size() << "\r\n\r\n"
                         << body;
            } else if (req.find("GET /api/sources") != std::string::npos) {
                nlohmann::json s;
                if (customCmdHandler) s = customCmdHandler("get_sources", nlohmann::json::object());
                else s = nlohmann::json::object();
                std::string body = s.dump();
                httpResp << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: application/json\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Content-Length: " << body.size() << "\r\n\r\n"
                         << body;
            } else {
                std::string body = "{\"status\": \"ok\", \"service\": \"SDR++ C++ Web Engine\"}";
                httpResp << "HTTP/1.1 200 OK\r\n"
                         << "Content-Type: application/json\r\n"
                         << "Access-Control-Allow-Origin: *\r\n"
                         << "Content-Length: " << body.size() << "\r\n\r\n"
                         << body;
            }

            std::string respStr = httpResp.str();
            send(client->sock, respStr.c_str(), (int)respStr.size(), 0);
        } else {
            // Process WebSocket Frames from Client
            while (client->rxBuffer.size() >= 2) {
                const uint8_t* p = (const uint8_t*)client->rxBuffer.data();
                uint8_t opcode = p[0] & 0x0F;
                bool masked = (p[1] & 0x80) != 0;
                uint64_t payloadLen = p[1] & 0x7F;
                size_t offset = 2;

                if (payloadLen == 126) {
                    if (client->rxBuffer.size() < 4) break;
                    payloadLen = ((uint64_t)p[2] << 8) | p[3];
                    offset = 4;
                } else if (payloadLen == 127) {
                    if (client->rxBuffer.size() < 10) break;
                    payloadLen = 0;
                    for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | p[2 + i];
                    offset = 10;
                }

                uint8_t maskKey[4] = {0, 0, 0, 0};
                if (masked) {
                    if (client->rxBuffer.size() < offset + 4) break;
                    memcpy(maskKey, client->rxBuffer.data() + offset, 4);
                    offset += 4;
                }

                if (client->rxBuffer.size() < offset + payloadLen) break;

                // Extract payload
                std::string payload;
                payload.resize(payloadLen);
                for (size_t i = 0; i < payloadLen; i++) {
                    uint8_t b = (uint8_t)client->rxBuffer[offset + i];
                    payload[i] = masked ? (b ^ maskKey[i % 4]) : b;
                }

                client->rxBuffer.erase(0, offset + payloadLen);

                // Handle Close / Ping / Text Command
                if (opcode == 0x08) {
                    closesocket(client->sock);
                    client->sock = INVALID_SOCKET;
                    break;
                } else if (opcode == 0x09) {
                    sendWsFrame(client->sock, 0x0A, (const uint8_t*)payload.data(), payload.size());
                } else if (opcode == 0x01) {
                    // JSON command from WebUI
                    try {
                        auto j = nlohmann::json::parse(payload);
                        std::string cmd = j.value("cmd", "");
                        flog::info("Received WebUI Command: {0}", cmd);

                        nlohmann::json resp;
                        if (cmd == "get_status") {
                            resp = getStatusJson();
                        } else if (cmd == "start") {
                            sigpath::sourceManager.start();
                            resp["status"] = "ok";
                            resp["running"] = true;
                        } else if (cmd == "stop") {
                            sigpath::sourceManager.stop();
                            resp["status"] = "ok";
                            resp["running"] = false;
                        } else if (cmd == "set_freq" && j.contains("freq")) {
                            double freq = j["freq"];
                            sigpath::sourceManager.tune(freq);
                            resp["status"] = "ok";
                            resp["freq"] = freq;
                        } else if (customCmdHandler) {
                            resp = customCmdHandler(cmd, j);
                        } else {
                            resp["status"] = "ok";
                        }

                        std::string respStr = resp.dump();
                        sendWsFrame(client->sock, 0x01, (const uint8_t*)respStr.data(), respStr.size());
                    } catch (const std::exception& e) {
                        flog::warn("Invalid WebUI JSON command: {0}", e.what());
                    }
                }
            }
        }
    }

    // === Main Web Server Socket Loop ===
    void serverWorker(int port, std::string host) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) {
            flog::error("Failed to create Web Server socket");
            return;
        }

        int opt = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host.c_str());

        if (bind(listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            flog::error("Failed to bind Web Server to {0}:{1}", host, port);
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return;
        }

        if (listen(listenSocket, 10) == SOCKET_ERROR) {
            flog::error("Failed to listen on Web Server port {0}", port);
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            return;
        }

        flog::info("⚡ High-Performance WebSocket & HTTP Server listening on {0}:{1}", host, port);

        char buf[8192];
        while (serverRunning) {
            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(listenSocket, &readFds);
            SOCKET maxFd = listenSocket;

            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                for (auto& c : clients) {
                    if (c->sock != INVALID_SOCKET) {
                        FD_SET(c->sock, &readFds);
                        if (c->sock > maxFd) maxFd = c->sock;
                    }
                }
            }

            timeval tv = {0, 50000}; // 50ms timeout
            int sel = select((int)maxFd + 1, &readFds, NULL, NULL, &tv);
            if (sel <= 0) continue;

            // New incoming connection
            if (FD_ISSET(listenSocket, &readFds)) {
                sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);
                SOCKET clientSock = accept(listenSocket, (sockaddr*)&clientAddr, &clientLen);
                if (clientSock != INVALID_SOCKET) {
                    auto newClient = std::make_shared<WsClient>();
                    newClient->sock = clientSock;
                    newClient->isWebSocket = false;
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    clients.push_back(newClient);
                }
            }

            // Existing clients read
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                for (auto it = clients.begin(); it != clients.end();) {
                    auto client = *it;
                    if (client->sock != INVALID_SOCKET && FD_ISSET(client->sock, &readFds)) {
                        int r = recv(client->sock, buf, sizeof(buf) - 1, 0);
                        if (r > 0) {
                            buf[r] = '\0';
                            handleIncomingData(client, buf, r);
                            ++it;
                        } else {
                            closesocket(client->sock);
                            client->sock = INVALID_SOCKET;
                            it = clients.erase(it);
                        }
                    } else if (client->sock == INVALID_SOCKET) {
                        it = clients.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        if (listenSocket != INVALID_SOCKET) {
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
        }
    }

    void start(int port, const std::string& host) {
        if (serverRunning) return;
        initFft();
        serverRunning = true;
        serverThread = std::thread(serverWorker, port, host);
    }

    void stop() {
        if (!serverRunning) return;
        serverRunning = false;
        if (serverThread.joinable()) {
            serverThread.join();
        }
        cleanupFft();
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (auto& c : clients) {
            if (c->sock != INVALID_SOCKET) {
                closesocket(c->sock);
            }
        }
        clients.clear();
    }

    bool isRunning() {
        return serverRunning;
    }
}
