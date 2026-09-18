#pragma once

#include "dsp/FeedbackDelayNetwork.h"

#include <array>
#include <cstddef>
#include <memory>

namespace aetherfield::dsp {

// The fixed preparation-time specification for the DS-B evaluation baseline.
// It deliberately carries time-domain delay targets, never direct sample
// lengths, and the allpass coefficient is validation data rather than a
// runtime control. This Task 1 boundary owns configuration/lifecycle only;
// audio processing and stereo output follow in later tasks.
struct DiffusionStereoConfig {
    double sampleRate;
    std::size_t lineCount;
    double fdnMinDelaySeconds;
    double fdnMaxDelaySeconds;
    double t60ZeroSeconds;
    double t60PiSeconds;
    double dMaxDb;
    std::array<double, 4> inputDelaySeconds;
    std::array<double, 2> leftOutputDelaySeconds;
    std::array<double, 2> rightOutputDelaySeconds;
    double allpassCoefficient;
};

// The per-sample mono-input/stereo-output result for the DS-B evaluation
// baseline. Task 2b reports direct stage observations only; Task 3 owns their
// cumulative aggregation, latching, and recovery scheduling.
struct StereoSample {
    float left = 0.0F;
    float right = 0.0F;
    bool nonFinite = false;
};

// Owns every DS-B component's preparation and lifecycle. Task 1 intentionally
// has no audio process method: the read-only FDN pre-step tap view exists, but
// diffusion signal routing, stereo output and Mix behavior do not yet.
class DiffusionStereoPath {
public:
    DiffusionStereoPath() noexcept;
    ~DiffusionStereoPath();

    DiffusionStereoPath(const DiffusionStereoPath&) = delete;
    DiffusionStereoPath& operator=(const DiffusionStereoPath&) = delete;
    DiffusionStereoPath(DiffusionStereoPath&&) noexcept;
    DiffusionStereoPath& operator=(DiffusionStereoPath&&) noexcept;

    // Validates every member before replacing state, prepares a complete
    // candidate off the render thread, and commits it only after all component
    // preparations succeed. Validation failure returns false without changing
    // the existing state; allocation failure propagates with the same
    // preservation guarantee.
    bool prepare(const DiffusionStereoConfig& config);

    // Resets every owned recursive component and clears the wrapper's current
    // latch/recovery state. The aggregate counter is intentionally preserved.
    // Safe before preparation; allocation-free and noexcept.
    void reset() noexcept;

    bool isPrepared() const noexcept;
    std::size_t inputDelaySamples(std::size_t index) const noexcept;
    std::size_t leftOutputDelaySamples(std::size_t index) const noexcept;
    std::size_t rightOutputDelaySamples(std::size_t index) const noexcept;
    float allpassCoefficient() const noexcept;

    // Returns the owned FDN's current pre-step even/odd line sums without
    // advancing it. Before preparation this returns {0, 0}. Task 2 exposes
    // this view only; Task 2 does not add wrapper audio processing.
    PreStepTapSums preStepTapSums() const noexcept;

    // ---- Control-thread API (ADR-004) ----
    // Thin forwards to the owned ParameterAutomation's identically-named
    // setter -- see ParameterAutomation::setDecay()/setDamp()/setMix() for
    // the full validation/derivation/transport contract (a normalized
    // [0,1] target; a non-finite value is rejected and leaves the current
    // target unchanged; an out-of-range finite value is clamped). Each
    // returns false, unchanged, before preparation (no automation object
    // exists yet to set). Not real-time safe, matching the underlying
    // call; never called concurrently with itself.
    bool setDecay(double normalized) noexcept;
    bool setDamp(double normalized) noexcept;
    bool setMix(double normalized) noexcept;

    // Runs one mono input sample through the input allpass cascade, normalized
    // FDN injection, pre-step even/odd taps, output allpass cascades and Mix.
    // It advances automation and the FDN exactly once. Safe before preparation:
    // returns silent output without allocating or changing lifecycle state.
    // Task 3 adds aggregate detector and block-boundary recovery behavior.
    StereoSample processSample(float mono) noexcept;

    // Processes one mono block into independent left/right output buffers.
    // A zero-frame call is a no-op and may use null buffers. For a nonempty
    // block, a pending recovery resets the full path before its first sample,
    // then automation target transport is observed once at the block boundary.
    void process(const float* mono, float* left, float* right, std::size_t count) noexcept;

    // Task 1 establishes the aggregate detector's storage/lifecycle boundary.
    // Task 3 adds per-sample event observation and reset scheduling.
    std::size_t nonFiniteCount() const noexcept;
    bool nonFiniteLatched() const noexcept;
    bool resetPending() const noexcept;

#if defined(AETHERFIELD_TESTING)
    // Test-only forwarding seam for the FDN counter-saturation contract.
    void setFdnNonFiniteStateForTest(std::size_t count, bool latched) noexcept;
#endif

private:
    StereoSample processOne(float mono) noexcept;
    void observeFault() noexcept;
    bool observeFdnFault(std::size_t beforeCount, bool beforeLatched,
                         bool upstreamFaultObserved) noexcept;

    struct State;
    std::unique_ptr<State> state_;
    std::size_t nonFiniteCount_ = 0;
    bool nonFiniteLatched_ = false;
    bool resetPending_ = false;
};

} // namespace aetherfield::dsp
