#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <dsp/types.h>
#include <dsp/stream.h>
#include <json.hpp>

namespace web_server {
    void start(int port = 5259, const std::string& host = "0.0.0.0");
    void stop();
    bool isRunning();

    // Broadcast binary Float32Array FFT power spectrum to all connected WebUI clients
    void broadcastFft(const float* fftDb, int size);

    // Broadcast decoded packet JSON to all connected WebUI clients
    void broadcastPacket(const nlohmann::json& packet);

    // Feed raw IQ samples from C++ DSP engine to calculate FFT & broadcast
    void processIqSamples(const dsp::complex_t* samples, int count, double sampleRate);

    // Handler for incoming commands from WebUI (JSON or REST)
    typedef std::function<nlohmann::json(const std::string& cmd, const nlohmann::json& params)> CommandHandler;
    void setCommandHandler(CommandHandler handler);

    // Get current server status JSON
    nlohmann::json getStatusJson();
}
