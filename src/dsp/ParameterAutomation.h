#pragma once

#include "dsp/FeedbackDelayNetwork.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aetherfield::dsp {

// Validation, coefficient derivation (ADR-004 (b)), lock-free single-
// writer/single-reader transport with a generation counter (ADR-004 (d)),
// and render-thread linear-ramp smoothing (ADR-004 (c)) for Aetherfield's
// three accepted controls: Mix, Decay, Damp. Automates an already-prepared
// FeedbackDelayNetwork via its setLineGain()/setDampingCoefficientC().
// See docs/decisions.md ADR-004, docs/phase1-pt-plan.md.
class ParameterAutomation {
public:
    ParameterAutomation() noexcept;

    // Control-thread-and-preparation-time call, off the render thread.
    // `network` MUST already be successfully prepared: this call reads
    // its lineCount() and delaySamples(i) directly rather than accepting
    // a second, independently-supplied copy that could drift out of sync
    // with it. `sampleRate` MUST match the value `network` was itself
    // prepared with (T60_min/T60_max are rate-dependent). dMaxDb is this
    // plan's test-fixture value (48.0) or another explicitly labeled
    // non-product value; never a value presented as a product decision.
    //
    // On success: computes T60_min/T60_max (docs/phase1-pt-plan.md's
    // closed forms) from network's own m_min/m_max, sizes the transport's
    // atomic arrays and the render-thread ramps to
    // 2*network.lineCount() + 2 elements, sets every control to its
    // default (Decay = 0.5, Damp = 0, Mix = 1.0 - full wet, matching S2's
    // own fixture convention of no dry path), publishes an initial
    // generation, and leaves the object in exactly the state reset()
    // defines. Not noexcept: allocation failure propagates.
    //
    // Preconditions, each independently checked, each causing prepare()
    // to return false with no allocation and no change to any prior valid
    // preparation: network.lineCount() > 0 (i.e. network is itself
    // prepared); sampleRate finite > 0.0; dMaxDb finite and >= 0.0
    // (ADR-004: every D >= 0 is stable; a negative D is not a "less
    // damping" request the mapping can express, since D(h) = D_max * h
    // with h in [0,1] cannot go negative).
    bool prepare(const FeedbackDelayNetwork& network, double sampleRate, double dMaxDb);

    // ---- Control-thread API (ADR-004 (b), (d)) ----
    // Sets a new normalized [0,1] target. A non-finite value is REJECTED:
    // the published coefficient set and generation are left unchanged,
    // and false is returned. A finite out-of-range value is CLAMPED to
    // [0,1] and accepted. On acceptance: derives the full coefficient set
    // in double from the (possibly just-clamped) value and the other two
    // controls' last-set values, per ADR-004 (b)'s exact formulas, stores
    // each derived target into the atomic array, and release-publishes an
    // incremented generation. Not real-time safe (uses double math and is
    // meant to run on a control/UI thread); never called concurrently
    // with itself (single-writer, per ADR-004 (d)).
    bool setDecay(double normalized) noexcept;
    bool setDamp(double normalized) noexcept;
    bool setMix(double normalized) noexcept;

    // ---- Render-thread API (ADR-004 (c), (d)) ----
    // Acquire-loads the generation. If it differs from the one last
    // consumed, copies each atomic target into the corresponding ramp as
    // a NEW target - starting a fresh rampLengthSamples_-sample ramp from
    // each ramp's CURRENT value (ADR-004 (c) point 3), never resetting a
    // ramp already in flight toward a still-current target. On an
    // unchanged generation this call performs only the one atomic load.
    // Safe to call once per block or once per sample; both are correct.
    void checkForNewTargets() noexcept;

    // Advances every one of the 2*lineCount()+2 smoothers by exactly one
    // sample (one add per coefficient, unconditional, per ADR-004 (c)
    // point 1 - zero when settled, no branch on ramp state), applies the
    // resulting per-line gain/damping coefficient to `network` via its
    // setLineGain()/setDampingCoefficientC(), and returns this sample's
    // dry/wet mix gains for the CALLER to apply outside network's own
    // process - Mix is structurally outside the feedback loop (ADR-004
    // (d) point 1) and FeedbackDelayNetwork itself has, and needs, no
    // concept of dry/wet.
    //
    // noexcept, allocation-free, lock-free, branch count independent of
    // sample values.
    struct MixGains {
        float dry;
        float wet;
    };
    MixGains advance(FeedbackDelayNetwork& network) noexcept;

    // Snaps every smoother to its current target and cancels any in-
    // flight ramp (ADR-004 (c) point 5). Does not touch `network` itself
    // - the caller is expected to reset() the network separately, exactly
    // as ADR-004 (c) point 5's silence-in/silence-out argument requires
    // both to happen together. Idempotent, safe before prepare() (a
    // no-op then), noexcept, allocation-free.
    void reset() noexcept;

    // Diagnostics.
    std::size_t nonFiniteRejectionCount() const noexcept;
    double t60Min() const noexcept;
    double t60Max() const noexcept;
    double dMax() const noexcept;

private:
    // One linear ramp: reaches target exactly by assignment after
    // rampLengthSamples steps from whatever value it held when
    // retargeted (ADR-004 (c) points 2-3). No separate "in flight" flag:
    // remaining == 0 and current == target are the same state.
    struct Ramp {
        float current = 0.0F;
        float target = 0.0F;
        float increment = 0.0F;
        std::size_t remaining = 0;
    };

    void startRamp(Ramp& ramp, float newTarget) noexcept;
    static void stepRamp(Ramp& ramp) noexcept;

    std::size_t lineCount_ = 0;
    std::vector<std::size_t> delaySamples_; // copied from network.delaySamples(i) at prepare()
    double sampleRate_ = 0.0;
    std::size_t rampLengthSamples_ = 0;
    double t60Min_ = 0.0;
    double t60Max_ = 0.0;
    double dMax_ = 0.0;
    double lastDecay_ = 0.5;
    double lastDamp_ = 0.0;
    double lastMix_ = 1.0;

    // Transport: N gains + N damping coefficients + dry + wet.
    std::vector<std::atomic<float>> targetGains_;
    std::vector<std::atomic<float>> targetDampingC_;
    std::atomic<float> targetDry_{0.0F};
    std::atomic<float> targetWet_{1.0F};
    std::atomic<std::uint64_t> generation_{0};
    std::uint64_t consumedGeneration_ = 0;

    // Render-thread smoothers, mirroring the transport 1:1.
    std::vector<Ramp> gainRamps_;
    std::vector<Ramp> dampingRamps_;
    Ramp dryRamp_;
    Ramp wetRamp_;

    // Fixed-capacity scratch for publish()'s derivation, avoiding a heap
    // allocation on every control-thread set*() call (lineCount_ is
    // always <= FeedbackDelayNetwork::kMaxLineCount once prepared).
    std::array<float, FeedbackDelayNetwork::kMaxLineCount> publishGainScratch_{};
    std::array<float, FeedbackDelayNetwork::kMaxLineCount> publishDampingScratch_{};

    std::size_t nonFiniteRejectionCount_ = 0;

    bool publish(std::size_t lineCount, double decay, double damp, double mix) noexcept;
};

} // namespace aetherfield::dsp
