#pragma once
#include <string>
#include <vector>
#include "frame_detector.h"

namespace flrc {

class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;
    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;
    virtual void processFrame(const DecodedFrame& frame) = 0;
    virtual void renderUI() = 0;
    virtual void reset() = 0;
};

} // namespace flrc
