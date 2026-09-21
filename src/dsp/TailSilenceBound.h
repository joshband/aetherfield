#pragma once

#include <array>
#include <cstddef>

namespace aetherfield::dsp {

struct TailSilenceBoundInput {
    double sampleRate = 0.0;
    double t60Min = 0.0;
    double t60Max = 0.0;
    double decay = 0.0;
    float inputEnvelope = 0.0F;
    float allpassCoefficient = 0.0F;
    std::size_t lineCount = 0;
    std::size_t minFdnDelay = 0;
    std::size_t maxFdnDelay = 0;
    std::array<std::size_t, 4> inputDelays {};
    std::array<std::size_t, 2> leftOutputDelays {};
    std::array<std::size_t, 2> rightOutputDelays {};
};

// Returns zero for a silent envelope or an invalid/unrepresentable bound.
// Callers must treat a non-silent zero result as an infinite bound.
std::size_t calculateTailSilenceBoundSamples(const TailSilenceBoundInput& input) noexcept;

} // namespace aetherfield::dsp
