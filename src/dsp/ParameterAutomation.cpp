#include "dsp/ParameterAutomation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aetherfield::dsp {

namespace {

constexpr double kAMax = 0.999;           // ADR-003 (a)
constexpr double kTauRampSeconds = 0.020; // ADR-004 (c): tau_ramp = 20 ms

double clamp01(double x) {
    return std::min(1.0, std::max(0.0, x));
}

} // namespace

ParameterAutomation::ParameterAutomation() noexcept = default;

bool ParameterAutomation::prepare(const FeedbackDelayNetwork& network, double sampleRate, double dMaxDb) {
    const std::size_t lineCount = network.lineCount();
    if (lineCount == 0) {
        return false; // network is not itself prepared
    }
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return false;
    }
    if (!std::isfinite(dMaxDb) || dMaxDb < 0.0) {
        return false;
    }

    std::vector<std::size_t> newDelaySamples(lineCount);
    for (std::size_t i = 0; i < lineCount; ++i) {
        newDelaySamples[i] = network.delaySamples(i);
    }
    const std::size_t mMin = newDelaySamples.front();
    const std::size_t mMax = newDelaySamples.back();

    // Closed forms, docs/phase1-pt-plan.md, evaluated from ADR-003/ADR-004's
    // already-decided rules for this exact delay set.
    constexpr double deltaMargin = 1e-3; // ADR-003 (c)
    const double t60Max = (3.0 * static_cast<double>(mMin)) / (sampleRate * (-std::log10(1.0 - deltaMargin)));
    const double floatMin = static_cast<double>(std::numeric_limits<float>::min());
    const double t60Min = (3.0 * static_cast<double>(mMax)) / (sampleRate * (-std::log10(floatMin)));

    if (!std::isfinite(t60Max) || !std::isfinite(t60Min) || t60Min <= 0.0 || t60Max <= t60Min) {
        return false;
    }

    const std::size_t rampLength =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(kTauRampSeconds * sampleRate)));

    // Commit.
    lineCount_ = lineCount;
    delaySamples_ = std::move(newDelaySamples);
    sampleRate_ = sampleRate;
    rampLengthSamples_ = rampLength;
    t60Min_ = t60Min;
    t60Max_ = t60Max;
    dMax_ = dMaxDb;

    targetGains_ = std::vector<std::atomic<float>>(lineCount_);
    targetDampingC_ = std::vector<std::atomic<float>>(lineCount_);
    gainRamps_.assign(lineCount_, Ramp{});
    dampingRamps_.assign(lineCount_, Ramp{});

    lastDecay_ = 0.5;
    lastDamp_ = 0.0;
    lastMix_ = 1.0;
    nonFiniteRejectionCount_ = 0;

    // consumedGeneration_ starts at 0 and generation_ is fetch_add'd from 0
    // to 1 by publish() below, so checkForNewTargets() will already see a
    // fresh generation without any extra bookkeeping here.
    if (!publish(lineCount_, lastDecay_, lastDamp_, lastMix_)) {
        return false;
    }

    // Populate every ramp's .target from the publish above, then snap
    // .current to it: this is what makes prepare()'s postcondition
    // literally "the object is left in exactly the state reset() defines"
    // rather than leaving reset() to snap ramps to a stale default target.
    checkForNewTargets();
    reset();
    return true;
}

bool ParameterAutomation::publish(std::size_t lineCount, double decay, double damp, double mix) noexcept {
    // ADR-004 (b): "T60_0(0) = T60_min and T60_0(1) = T60_max assigned
    // exactly, not evaluated" — pow(ratio, 1.0) returns ratio exactly per
    // IEEE 754, but t60Min_ * (t60Max_ / t60Min_) is not guaranteed to
    // round-trip back to exactly t60Max_, so d=1 is assigned directly
    // rather than evaluated through the general formula.
    double t60Zero;
    if (decay <= 0.0) {
        t60Zero = t60Min_;
    } else if (decay >= 1.0) {
        t60Zero = t60Max_;
    } else {
        t60Zero = t60Min_ * std::pow(t60Max_ / t60Min_, decay);
    }
    const double excessDb = dMax_ * damp; // D(h); exact at h=0 and h=1 already (multiply by 0.0 or 1.0)
    const double t60Pi = t60Zero / (1.0 + excessDb / 60.0);

    const double gamma0 = std::pow(10.0, -3.0 / (sampleRate_ * t60Zero));
    const double gammaPi = std::pow(10.0, -3.0 / (sampleRate_ * t60Pi));

    for (std::size_t i = 0; i < lineCount; ++i) {
        const double m = static_cast<double>(delaySamples_[i]);
        const double gRaw = std::pow(gamma0, m);
        const double beta = std::pow(gammaPi / gamma0, m);
        double a = (1.0 - beta) / (1.0 + beta);
        a = std::min(a, kAMax);
        a = std::max(a, 0.0);
        const double c = 1.0 - a;
        const double gFolded = gRaw / std::sqrt(static_cast<double>(lineCount));

        if (!std::isfinite(gFolded) || !std::isfinite(c)) {
            return false;
        }
        publishGainScratch_[i] = static_cast<float>(gFolded);
        publishDampingScratch_[i] = static_cast<float>(c);
    }

    // ADR-004 (b): "dry(0) = 1, wet(0) = 0, dry(1) = 0, wet(1) = 1 assigned
    // exactly" — cos(pi/2) and sin(0) are not exactly 0 in floating point
    // (M_PI is a finite approximation of an irrational number), so both
    // endpoints are assigned directly rather than evaluated through the
    // trig formula.
    double dry;
    double wet;
    if (mix <= 0.0) {
        dry = 1.0;
        wet = 0.0;
    } else if (mix >= 1.0) {
        dry = 0.0;
        wet = 1.0;
    } else {
        dry = std::cos(M_PI * mix / 2.0);
        wet = std::sin(M_PI * mix / 2.0);
    }
    if (!std::isfinite(dry) || !std::isfinite(wet)) {
        return false;
    }

    // All derived values validated: publish.
    for (std::size_t i = 0; i < lineCount; ++i) {
        targetGains_[i].store(publishGainScratch_[i], std::memory_order_relaxed);
        targetDampingC_[i].store(publishDampingScratch_[i], std::memory_order_relaxed);
    }
    targetDry_.store(static_cast<float>(dry), std::memory_order_relaxed);
    targetWet_.store(static_cast<float>(wet), std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_release);
    return true;
}

bool ParameterAutomation::setDecay(double normalized) noexcept {
    if (!std::isfinite(normalized)) {
        ++nonFiniteRejectionCount_;
        return false;
    }
    const double clamped = clamp01(normalized);
    if (!publish(lineCount_, clamped, lastDamp_, lastMix_)) {
        return false;
    }
    lastDecay_ = clamped;
    return true;
}

bool ParameterAutomation::setDamp(double normalized) noexcept {
    if (!std::isfinite(normalized)) {
        ++nonFiniteRejectionCount_;
        return false;
    }
    const double clamped = clamp01(normalized);
    if (!publish(lineCount_, lastDecay_, clamped, lastMix_)) {
        return false;
    }
    lastDamp_ = clamped;
    return true;
}

bool ParameterAutomation::setMix(double normalized) noexcept {
    if (!std::isfinite(normalized)) {
        ++nonFiniteRejectionCount_;
        return false;
    }
    const double clamped = clamp01(normalized);
    if (!publish(lineCount_, lastDecay_, lastDamp_, clamped)) {
        return false;
    }
    lastMix_ = clamped;
    return true;
}

void ParameterAutomation::checkForNewTargets() noexcept {
    const std::uint64_t generation = generation_.load(std::memory_order_acquire);
    if (generation == consumedGeneration_) {
        return;
    }
    consumedGeneration_ = generation;

    for (std::size_t i = 0; i < lineCount_; ++i) {
        startRamp(gainRamps_[i], targetGains_[i].load(std::memory_order_relaxed));
        startRamp(dampingRamps_[i], targetDampingC_[i].load(std::memory_order_relaxed));
    }
    startRamp(dryRamp_, targetDry_.load(std::memory_order_relaxed));
    startRamp(wetRamp_, targetWet_.load(std::memory_order_relaxed));
}

void ParameterAutomation::startRamp(Ramp& ramp, float newTarget) noexcept {
    ramp.target = newTarget;
    ramp.remaining = rampLengthSamples_;
    ramp.increment = (ramp.target - ramp.current) / static_cast<float>(rampLengthSamples_);
}

void ParameterAutomation::stepRamp(Ramp& ramp) noexcept {
    if (ramp.remaining == 0) {
        return;
    }
    --ramp.remaining;
    if (ramp.remaining == 0) {
        ramp.current = ramp.target; // exact by assignment, never asymptotic (ADR-004 (c) point 2)
    } else {
        ramp.current += ramp.increment;
    }
}

ParameterAutomation::MixGains ParameterAutomation::advance(FeedbackDelayNetwork& network) noexcept {
    for (std::size_t i = 0; i < lineCount_; ++i) {
        stepRamp(gainRamps_[i]);
        stepRamp(dampingRamps_[i]);
        network.setLineGain(i, gainRamps_[i].current);
        network.setDampingCoefficientC(i, dampingRamps_[i].current);
    }
    stepRamp(dryRamp_);
    stepRamp(wetRamp_);
    return MixGains{dryRamp_.current, wetRamp_.current};
}

void ParameterAutomation::reset() noexcept {
    for (std::size_t i = 0; i < lineCount_; ++i) {
        gainRamps_[i].current = gainRamps_[i].target;
        gainRamps_[i].remaining = 0;
        dampingRamps_[i].current = dampingRamps_[i].target;
        dampingRamps_[i].remaining = 0;
    }
    dryRamp_.current = dryRamp_.target;
    dryRamp_.remaining = 0;
    wetRamp_.current = wetRamp_.target;
    wetRamp_.remaining = 0;
}

std::size_t ParameterAutomation::nonFiniteRejectionCount() const noexcept {
    return nonFiniteRejectionCount_;
}

double ParameterAutomation::t60Min() const noexcept {
    return t60Min_;
}

double ParameterAutomation::t60Max() const noexcept {
    return t60Max_;
}

double ParameterAutomation::dMax() const noexcept {
    return dMax_;
}

} // namespace aetherfield::dsp
