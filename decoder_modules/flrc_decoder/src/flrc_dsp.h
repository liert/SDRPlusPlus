#pragma once
#include <dsp/processor.h>
#include <dsp/types.h>
#include <dsp/taps/low_pass.h>
#include <dsp/taps/windowed_sinc.h>
#include <dsp/filter/fir.h>
#include <vector>
#include <cmath>
#include <mutex>
#include "flrc_config.h"

namespace flrc {

class FLRCDSP : public dsp::Processor<dsp::complex_t, float> {
    using base_type = dsp::Processor<dsp::complex_t, float>;
public:
    FLRCDSP();
    ~FLRCDSP();

    void init(dsp::stream<dsp::complex_t>* in, const DemodConfig& config);
    void setConfig(const DemodConfig& config);
    void reset();

    // Direct block processing
    int process(int count, const dsp::complex_t* in, float* out);
    int run();

    // Stream for eye / symbol diagram
    dsp::stream<float> diagOut;

private:
    void updateFilter();

    DemodConfig _config;
    dsp::tap<float> _lpfTaps;
    dsp::complex_t _lastSample = {0.0f, 0.0f};

    // Filter buffer for FIR
    std::vector<dsp::complex_t> _filterHistory;
    std::vector<float> _taps;
};

} // namespace flrc
