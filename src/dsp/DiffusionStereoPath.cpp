#include "dsp/DiffusionStereoPath.h"

#include "dsp/FeedbackDelayNetwork.h"
#include "dsp/ParameterAutomation.h"
#include "dsp/SchroederAllpass.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace aetherfield::dsp {
namespace {

constexpr double kGoldenCoefficient = 0.6180339887498948482;
constexpr double kMaximumTargetSamples = static_cast<double>(std::numeric_limits<int>::max() - 1024);

bool validPositiveFinite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool validLineCount(std::size_t lineCount) noexcept {
    return lineCount == 4 || lineCount == 8 || lineCount == 16;
}

template <std::size_t Size>
bool validDelayTargets(const std::array<double, Size>& targets) noexcept {
    for (const double target : targets) {
        if (!validPositiveFinite(target)) return false;
    }
    return true;
}

bool hasValidDerivedTarget(double sampleRate, double seconds) noexcept {
    const double target = sampleRate * seconds;
    return std::isfinite(target) && target >= 1.0 && target <= kMaximumTargetSamples;
}

template <std::size_t Size>
bool hasValidDerivedTargets(double sampleRate, const std::array<double, Size>& targets) noexcept {
    for (const double seconds : targets) {
        if (!hasValidDerivedTarget(sampleRate, seconds)) return false;
    }
    return true;
}

bool isValidConfig(const DiffusionStereoConfig& config) noexcept {
    if (!validPositiveFinite(config.sampleRate) || !validLineCount(config.lineCount)
        || !validPositiveFinite(config.fdnMinDelaySeconds)
        || !validPositiveFinite(config.fdnMaxDelaySeconds)
        || config.fdnMinDelaySeconds >= config.fdnMaxDelaySeconds
        || !validPositiveFinite(config.t60ZeroSeconds)
        || !validPositiveFinite(config.t60PiSeconds)
        || config.t60PiSeconds > config.t60ZeroSeconds
        || !std::isfinite(config.dMaxDb) || config.dMaxDb < 0.0
        || !std::isfinite(config.allpassCoefficient)
        || !validDelayTargets(config.inputDelaySeconds)
        || !validDelayTargets(config.leftOutputDelaySeconds)
        || !validDelayTargets(config.rightOutputDelaySeconds)
        || !hasValidDerivedTarget(config.sampleRate, config.fdnMinDelaySeconds)
        || !hasValidDerivedTarget(config.sampleRate, config.fdnMaxDelaySeconds)
        || !hasValidDerivedTargets(config.sampleRate, config.inputDelaySeconds)
        || !hasValidDerivedTargets(config.sampleRate, config.leftOutputDelaySeconds)
        || !hasValidDerivedTargets(config.sampleRate, config.rightOutputDelaySeconds)) {
        return false;
    }

    return static_cast<float>(config.allpassCoefficient) == static_cast<float>(kGoldenCoefficient);
}

} // namespace

struct DiffusionStereoPath::State {
    std::array<SchroederAllpass, 4> input;
    std::array<SchroederAllpass, 2> leftOutput;
    std::array<SchroederAllpass, 2> rightOutput;
    FeedbackDelayNetwork network;
    ParameterAutomation automation;
    float coefficient = 0.0F;
};

namespace {

bool strictlyIncreasing(const std::array<std::size_t, 4>& delays) noexcept {
    for (std::size_t index = 1; index < delays.size(); ++index) {
        if (delays[index - 1] >= delays[index]) return false;
    }
    return true;
}

} // namespace

DiffusionStereoPath::DiffusionStereoPath() noexcept = default;
DiffusionStereoPath::~DiffusionStereoPath() = default;
DiffusionStereoPath::DiffusionStereoPath(DiffusionStereoPath&&) noexcept = default;
DiffusionStereoPath& DiffusionStereoPath::operator=(DiffusionStereoPath&&) noexcept = default;

bool DiffusionStereoPath::prepare(const DiffusionStereoConfig& config) {
    if (!isValidConfig(config)) return false;

    auto candidate = std::make_unique<State>();
    candidate->coefficient = static_cast<float>(config.allpassCoefficient);
    for (std::size_t index = 0; index < candidate->input.size(); ++index) {
        if (!candidate->input[index].prepare(config.sampleRate, config.inputDelaySeconds[index], candidate->coefficient)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < candidate->leftOutput.size(); ++index) {
        if (!candidate->leftOutput[index].prepare(config.sampleRate, config.leftOutputDelaySeconds[index], candidate->coefficient)
            || !candidate->rightOutput[index].prepare(config.sampleRate, config.rightOutputDelaySeconds[index], candidate->coefficient)) {
            return false;
        }
    }
    if (!candidate->network.prepare(config.sampleRate, config.lineCount,
                                    config.fdnMinDelaySeconds, config.fdnMaxDelaySeconds,
                                    config.t60ZeroSeconds, config.t60PiSeconds)) {
        return false;
    }

    const std::array<std::size_t, 4> input {
        candidate->input[0].delaySamples(), candidate->input[1].delaySamples(),
        candidate->input[2].delaySamples(), candidate->input[3].delaySamples(),
    };
    const std::array<std::size_t, 4> output {
        candidate->leftOutput[0].delaySamples(), candidate->rightOutput[0].delaySamples(),
        candidate->leftOutput[1].delaySamples(), candidate->rightOutput[1].delaySamples(),
    };
    const std::array<std::size_t, 8> diffusion {
        input[0], input[1], input[2], input[3], output[0], output[1], output[2], output[3],
    };
    bool validTopology = strictlyIncreasing(input) && strictlyIncreasing(output);
    for (std::size_t first = 0; validTopology && first < diffusion.size(); ++first) {
        for (std::size_t second = first + 1; second < diffusion.size(); ++second) {
            if (std::gcd(diffusion[first], diffusion[second]) != 1) validTopology = false;
        }
        for (std::size_t line = 0; validTopology && line < candidate->network.lineCount(); ++line) {
            if (std::gcd(diffusion[first], candidate->network.delaySamples(line)) != 1) validTopology = false;
        }
    }
    const std::size_t maxDiffusion = *std::max_element(diffusion.begin(), diffusion.end());
    if (!validTopology || maxDiffusion >= candidate->network.delaySamples(0)
        || !candidate->automation.prepare(candidate->network, config.sampleRate, config.dMaxDb)) {
        return false;
    }

    state_ = std::move(candidate);
    reset();
    return true;
}

void DiffusionStereoPath::reset() noexcept {
    if (state_) {
        for (auto& section : state_->input) section.reset();
        for (auto& section : state_->leftOutput) section.reset();
        for (auto& section : state_->rightOutput) section.reset();
        state_->network.reset();
        state_->automation.reset();
    }
    nonFiniteLatched_ = false;
    resetPending_ = false;
}

bool DiffusionStereoPath::isPrepared() const noexcept { return static_cast<bool>(state_); }

std::size_t DiffusionStereoPath::inputDelaySamples(std::size_t index) const noexcept {
    return state_ && index < state_->input.size() ? state_->input[index].delaySamples() : 0;
}

std::size_t DiffusionStereoPath::leftOutputDelaySamples(std::size_t index) const noexcept {
    return state_ && index < state_->leftOutput.size() ? state_->leftOutput[index].delaySamples() : 0;
}

std::size_t DiffusionStereoPath::rightOutputDelaySamples(std::size_t index) const noexcept {
    return state_ && index < state_->rightOutput.size() ? state_->rightOutput[index].delaySamples() : 0;
}

float DiffusionStereoPath::allpassCoefficient() const noexcept {
    return state_ ? state_->coefficient : 0.0F;
}

PreStepTapSums DiffusionStereoPath::preStepTapSums() const noexcept {
    return state_ ? state_->network.preStepTapSums() : PreStepTapSums {};
}

StereoSample DiffusionStereoPath::processSample(float mono) noexcept {
    if (!state_) return {};

    if (resetPending_) reset();
    state_->automation.checkForNewTargets();
    return processOne(mono);
}

void DiffusionStereoPath::process(const float* mono, float* left, float* right, std::size_t count) noexcept {
    if (count == 0 || !state_) return;

    if (resetPending_) reset();
    state_->automation.checkForNewTargets();
    for (std::size_t index = 0; index < count; ++index) {
        const StereoSample sample = processOne(mono[index]);
        left[index] = sample.left;
        right[index] = sample.right;
    }
}

StereoSample DiffusionStereoPath::processOne(float mono) noexcept {
    const ParameterAutomation::MixGains mix = state_->automation.advance(state_->network);

    bool nonFinite = !std::isfinite(mono);
    bool upstreamFaultObserved = nonFinite;
    if (nonFinite) observeFault();
    float diffused = nonFinite ? 0.0F : mono;
    for (auto& section : state_->input) {
        const SchroederAllpass::Sample stage = section.processSample(diffused);
        if (stage.nonFinite) {
            nonFinite = true;
            upstreamFaultObserved = true;
            observeFault();
        }
        diffused = stage.value;
    }

    const float inputScale = 1.0F / std::sqrt(static_cast<float>(state_->network.lineCount()));
    const float injection = diffused * inputScale;
    const PreStepTapSums taps = state_->network.preStepTapSums();
    const std::size_t fdnCountBefore = state_->network.nonFiniteCount();
    const bool fdnLatchedBefore = state_->network.nonFiniteLatched();
    static_cast<void>(state_->network.processSample(injection));
    nonFinite = observeFdnFault(fdnCountBefore, fdnLatchedBefore, upstreamFaultObserved) || nonFinite;

    const float tapScale = 1.0F / std::sqrt(static_cast<float>(state_->network.lineCount() / 2));
    float leftWet = taps.even * tapScale;
    float rightWet = taps.odd * tapScale;
    for (auto& section : state_->leftOutput) {
        const SchroederAllpass::Sample stage = section.processSample(leftWet);
        if (stage.nonFinite) {
            nonFinite = true;
            observeFault();
        }
        leftWet = stage.value;
    }
    for (auto& section : state_->rightOutput) {
        const SchroederAllpass::Sample stage = section.processSample(rightWet);
        if (stage.nonFinite) {
            nonFinite = true;
            observeFault();
        }
        rightWet = stage.value;
    }

    return {mix.dry * mono + mix.wet * leftWet, mix.dry * mono + mix.wet * rightWet, nonFinite};
}

void DiffusionStereoPath::observeFault() noexcept {
    if (nonFiniteCount_ < std::numeric_limits<std::size_t>::max()) {
        ++nonFiniteCount_;
    }
    nonFiniteLatched_ = true;
    resetPending_ = true;
}

bool DiffusionStereoPath::observeFdnFault(std::size_t beforeCount, bool beforeLatched,
                                          bool upstreamFaultObserved) noexcept {
    const std::size_t afterCount = state_->network.nonFiniteCount();
    const bool countAdvanced = afterCount >= beforeCount && afterCount != beforeCount;
    const bool latchTransition = !beforeLatched && state_->network.nonFiniteLatched();
    std::size_t delta = countAdvanced ? afterCount - beforeCount : 0;
    if (upstreamFaultObserved && delta > 0) {
        --delta; // The FDN's authorized non-finite input substitution is already observed upstream.
    }
    for (std::size_t index = 0; index < delta; ++index) observeFault();
    if (latchTransition && !countAdvanced) {
        observeFault(); // Latch-only observation: multiplicity is not known after FDN counter saturation.
    }
    return countAdvanced || latchTransition;
}

std::size_t DiffusionStereoPath::nonFiniteCount() const noexcept { return nonFiniteCount_; }
bool DiffusionStereoPath::nonFiniteLatched() const noexcept { return nonFiniteLatched_; }
bool DiffusionStereoPath::resetPending() const noexcept { return resetPending_; }

#if defined(AETHERFIELD_TESTING)
void DiffusionStereoPath::setFdnNonFiniteStateForTest(std::size_t count, bool latched) noexcept {
    if (state_) state_->network.setNonFiniteStateForTest(count, latched);
}
#endif

} // namespace aetherfield::dsp
