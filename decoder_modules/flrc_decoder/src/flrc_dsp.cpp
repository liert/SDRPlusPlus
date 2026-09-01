#define _USE_MATH_DEFINES
#include "flrc_dsp.h"
#include <dsp/math/constants.h>
#include <algorithm>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace flrc {

FLRCDSP::FLRCDSP() {}

FLRCDSP::~FLRCDSP() {
    if (!base_type::_block_init) { return; }
    base_type::stop();
    diagOut.free();
}

void FLRCDSP::init(dsp::stream<dsp::complex_t>* in, const DemodConfig& config) {
    _config = config;
    updateFilter();
    diagOut.init();
    base_type::init(in);
}

void FLRCDSP::setConfig(const DemodConfig& config) {
    assert(base_type::_block_init);
    std::lock_guard<std::recursive_mutex> lck(base_type::ctrlMtx);
    base_type::tempStop();
    _config = config;
    updateFilter();
    reset();
    base_type::tempStart();
}

void FLRCDSP::updateFilter() {
    int taps = _config.filterTaps;
    if (taps % 2 == 0) taps++; // Ensure odd number of taps
    _taps.resize(taps);
    _filterHistory.assign(taps, {0.0f, 0.0f});

    double cutoff = _config.filterCutoff / _config.sampleRate;
    double sum = 0.0;
    int M = taps - 1;
    for (int i = 0; i < taps; i++) {
        double n = i - M / 2.0;
        double sinc_val = (n == 0.0) ? (2.0 * cutoff) : (std::sin(2.0 * M_PI * cutoff * n) / (M_PI * n));
        double hamming = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / M);
        _taps[i] = (float)(sinc_val * hamming);
        sum += _taps[i];
    }
    for (int i = 0; i < taps; i++) {
        _taps[i] /= (float)sum;
    }
}

void FLRCDSP::reset() {
    _lastSample = {0.0f, 0.0f};
    std::fill(_filterHistory.begin(), _filterHistory.end(), dsp::complex_t{0.0f, 0.0f});
}

int FLRCDSP::process(int count, const dsp::complex_t* in, float* out) {
    if (count <= 0) return 0;

    int numTaps = (int)_taps.size();
    for (int i = 0; i < count; i++) {
        // Shift history and insert new sample
        for (int j = numTaps - 1; j > 0; j--) {
            _filterHistory[j] = _filterHistory[j - 1];
        }
        _filterHistory[0] = in[i];

        // Apply FIR filter
        dsp::complex_t filtered = {0.0f, 0.0f};
        for (int j = 0; j < numTaps; j++) {
            filtered.r += _filterHistory[j].r * _taps[j];
            filtered.i += _filterHistory[j].i * _taps[j];
        }

        // Phase discriminator: angle(s[n] * conj(s[n-1]))
        float dot = filtered.r * _lastSample.r + filtered.i * _lastSample.i;
        float cross = filtered.i * _lastSample.r - filtered.r * _lastSample.i;
        float angle = std::atan2(cross, dot);

        // Normalize output: positive -> +1 (bit 1), negative -> -1 (bit 0)
        out[i] = angle;
        _lastSample = filtered;
    }

    return count;
}

int FLRCDSP::run() {
    int count = base_type::_in->read();
    if (count < 0) { return -1; }

    int outCount = process(count, base_type::_in->readBuf, base_type::out.writeBuf);

    // Also forward a portion to diagOut for GUI visualization if available
    if (diagOut.writeBuf && outCount > 0) {
        int copyCount = std::min(outCount, 1024);
        memcpy(diagOut.writeBuf, base_type::out.writeBuf, copyCount * sizeof(float));
        diagOut.swap(copyCount);
    }

    base_type::_in->flush();
    if (outCount > 0) {
        if (!base_type::out.swap(outCount)) { return -1; }
    }
    return outCount;
}

} // namespace flrc
