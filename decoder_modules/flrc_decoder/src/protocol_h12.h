#pragma once
#include "protocol_base.h"
#include <array>
#include <string>
#include <vector>

namespace flrc {

enum class H12FrameType {
    UNKNOWN,
    NORMAL_CONTROL,
    NORMAL_MANAGEMENT,
    PAIRING_REQUEST,
    PAIRING_ACK,
    MAINTENANCE_CMD,
    MAINTENANCE_DATA,
    MAINTENANCE_RESP
};

struct H12ParsedData {
    H12FrameType frameType = H12FrameType::UNKNOWN;
    uint8_t hopIndex = 0;
    uint8_t groupIndex = 0;
    bool isManagement = false;
    bool transparentRoute = false;
    std::array<uint16_t, 12> channels = {480, 480, 480, 480, 480, 480, 480, 480, 480, 480, 480, 480};
    bool channelsValid = false;
    std::string transparentAscii;
    std::vector<uint8_t> transparentHex;
    uint8_t crc8Calculated = 0;
    uint8_t crc8Received = 0;
    bool crc8Valid = false;

    // Pairing info
    uint32_t pairingId = 0;
    std::vector<uint8_t> hopTable;

    // Maintenance
    uint8_t maintenanceCmd = 0;
};

class H12ProtocolHandler : public IProtocolHandler {
public:
    H12ProtocolHandler();
    ~H12ProtocolHandler() override = default;

    const char* getName() const override { return "H12 / T12 Drone Protocol"; }
    const char* getDescription() const override { return "H12 12-channel RC, telemetry, pairing & maintenance parser"; }

    void processFrame(const DecodedFrame& frame) override;
    void renderUI() override;
    void reset() override;

    static uint8_t crc8_smbus(const uint8_t* data, size_t len);

private:
    H12ParsedData parse(const std::vector<uint8_t>& payload);

    H12ParsedData lastParsed;
    bool hasData = false;
    uint64_t totalFrames = 0;
    uint64_t validCrc8Frames = 0;
    uint64_t pairingFrames = 0;
    uint64_t mgmtFrames = 0;
};

} // namespace flrc
