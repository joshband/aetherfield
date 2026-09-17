#pragma once

#include "dsp/DelayLine.h"

#include <cstddef>

namespace aetherfield::dsp {

// One fixed-delay Schroeder allpass section. Preparation derives and allocates
// the delay; processing reports exceptional values to the caller and does not
// silently replace them or reset its state.
class SchroederAllpass {
public:
    struct Sample {
        float value;
        bool nonFinite;
    };

    SchroederAllpass() noexcept = default;

    bool prepare(double sampleRate, double delaySeconds, double coefficient);
    Sample processSample(float input) noexcept;
    void reset() noexcept;

    std::size_t delaySamples() const noexcept;
    float coefficient() const noexcept;

private:
    DelayLine delay_;
    float coefficient_ = 0.0F;
    std::size_t delaySamples_ = 0;
};

} // namespace aetherfield::dsp
