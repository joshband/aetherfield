#include "dsp/SchroederAllpass.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace aetherfield::dsp {
namespace {

constexpr float kDenormalCutoff = 1e-20F;
constexpr std::int64_t kMaximumTargetSamples
    = static_cast<std::int64_t>(std::numeric_limits<int>::max()) - 1024;

bool isPrime(int value) noexcept {
    if (value < 2) return false;
    if (value == 2) return true;
    if (value % 2 == 0) return false;
    for (int divisor = 3; divisor <= value / divisor; divisor += 2) {
        if (value % divisor == 0) return false;
    }
    return true;
}

int nearestPrime(int roundedTarget) noexcept {
    for (int distance = 0;; ++distance) {
        const int lower = roundedTarget - distance;
        if (lower >= 2 && isPrime(lower)) return lower;
        if (distance == 0 || roundedTarget > std::numeric_limits<int>::max() - distance) continue;
        const int upper = roundedTarget + distance;
        if (isPrime(upper)) return upper;
    }
}

float applyCutoff(float value) noexcept {
    return std::fabs(value) >= kDenormalCutoff ? value : 0.0F;
}

} // namespace

bool SchroederAllpass::prepare(double sampleRate, double delaySeconds, double coefficient) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0
        || !std::isfinite(delaySeconds) || delaySeconds <= 0.0
        || !std::isfinite(coefficient) || coefficient < 0.0 || coefficient > 0.9) {
        return false;
    }

    const double target = sampleRate * delaySeconds;
    if (!std::isfinite(target) || target < 1.0
        || target > static_cast<double>(kMaximumTargetSamples)) {
        return false;
    }
    const int roundedTarget = static_cast<int>(std::llround(target));
    const int prime = nearestPrime(roundedTarget);
    if (prime < 2) return false;

    DelayLine candidate;
    if (!candidate.prepare(sampleRate, static_cast<std::size_t>(prime))) return false;
    delay_ = std::move(candidate);
    coefficient_ = static_cast<float>(coefficient);
    delaySamples_ = static_cast<std::size_t>(prime);
    return true;
}

SchroederAllpass::Sample SchroederAllpass::processSample(float input) noexcept {
    const float delayed = delay_.peek();
    const float state = input + coefficient_ * delayed;
    const float output = delayed - coefficient_ * state;
    const bool nonFinite = !std::isfinite(input) || !std::isfinite(delayed)
        || !std::isfinite(state) || !std::isfinite(output);
    const float stored = std::isfinite(state) ? applyCutoff(state) : state;
    delay_.push(stored);
    return {output, nonFinite};
}

void SchroederAllpass::reset() noexcept { delay_.reset(); }

std::size_t SchroederAllpass::delaySamples() const noexcept { return delaySamples_; }

float SchroederAllpass::coefficient() const noexcept { return coefficient_; }

} // namespace aetherfield::dsp
