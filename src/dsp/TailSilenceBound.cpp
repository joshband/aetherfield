#include "dsp/TailSilenceBound.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aetherfield::dsp {
namespace {

constexpr double kEpsilon = 1e-20;
constexpr std::size_t kMaximum = std::numeric_limits<std::size_t>::max();

std::size_t saturatingAdd(std::size_t left, std::size_t right) noexcept {
    return right > kMaximum - left ? kMaximum : left + right;
}

std::size_t ceilToSize(double value) noexcept {
    if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(kMaximum)) return 0;
    return static_cast<std::size_t>(std::ceil(value));
}

std::size_t stageDrain(double amplitude, double q, std::size_t delay) noexcept {
    if (!(amplitude > 0.0) || !(q > 0.0 && q < 1.0) || delay == 0) return 0;
    const double circuits = std::max(1.0, std::ceil(std::log(kEpsilon / amplitude) / std::log(q)));
    const std::size_t count = ceilToSize(circuits + 1.0);
    if (count == 0 || delay > kMaximum / count) return 0;
    return count * delay;
}

template <std::size_t Count>
std::size_t cascadeDrain(double sourceBound, double peakGain, double q,
                          const std::array<std::size_t, Count>& delays) noexcept {
    std::size_t total = 0;
    for (std::size_t index = 0; index < delays.size(); ++index) {
        const double stateBound = sourceBound * std::pow(peakGain, static_cast<double>(index)) / (1.0 - q);
        const std::size_t drain = stageDrain(stateBound, q, delays[index]);
        if (drain == 0) return 0;
        total = saturatingAdd(total, drain);
    }
    return total;
}

} // namespace

std::size_t calculateTailSilenceBoundSamples(const TailSilenceBoundInput& input) noexcept {
    const double p = std::fabs(static_cast<double>(input.inputEnvelope));
    if (p == 0.0F) return 0;
    const double q = static_cast<double>(input.allpassCoefficient);
    if (!std::isfinite(p) || !std::isfinite(input.sampleRate) || !std::isfinite(input.t60Min)
        || !std::isfinite(input.t60Max) || !std::isfinite(input.decay) || !(input.sampleRate > 0.0)
        || !(input.t60Min > 0.0) || !(input.t60Max >= input.t60Min) || !(input.decay >= 0.0 && input.decay <= 1.0)
        || !(q > 0.0 && q < 1.0) || input.lineCount == 0 || input.minFdnDelay == 0 || input.maxFdnDelay == 0) {
        return 0;
    }
    const double t60 = input.t60Min * std::pow(input.t60Max / input.t60Min, input.decay);
    const double rho = std::pow(10.0, -3.0 * static_cast<double>(input.minFdnDelay) / (input.sampleRate * t60));
    const double peakGain = 1.0 + 2.0 * q;
    const double liveStateBound = p * std::pow(peakGain, 4.0) / (1.0 - rho);
    if (!std::isfinite(t60) || !(rho > 0.0 && rho < 1.0) || !std::isfinite(liveStateBound) || !(liveStateBound > 0.0)) {
        return 0;
    }
    const std::size_t inputDrain = cascadeDrain(p, peakGain, q, input.inputDelays);
    const std::size_t leftDrain = cascadeDrain(liveStateBound, peakGain, q, input.leftOutputDelays);
    const std::size_t rightDrain = cascadeDrain(liveStateBound, peakGain, q, input.rightOutputDelays);
    const double fdnSamplesDouble = static_cast<double>(input.maxFdnDelay)
        * std::log(kEpsilon / liveStateBound) / std::log(rho);
    const std::size_t fdnDrain = ceilToSize(std::max(0.0, fdnSamplesDouble));
    if (inputDrain == 0 || leftDrain == 0 || rightDrain == 0 || fdnDrain == 0) return 0;
    return saturatingAdd(saturatingAdd(inputDrain, fdnDrain), std::max(leftDrain, rightDrain));
}

} // namespace aetherfield::dsp
