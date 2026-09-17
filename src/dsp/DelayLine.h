#pragma once

#include <cstddef>
#include <vector>

namespace aetherfield::dsp {

// A single fixed-length delay line: y[n] = x[n - delaySamples].
// No feedback, no matrix, no damping, no runtime-adjustable length.
// See docs/decisions.md ADR-002, milestone S1.
class DelayLine {
public:
    // Constructs an unprepared object: zero capacity, no allocation.
    DelayLine() noexcept;

    // The only allocation point (ADR-002: "no allocation on the render
    // thread ... all delay storage fixed during preparation, off the
    // render thread"). Must be called off the render thread, before any
    // process()/reset() call that should observe the new configuration.
    //
    // sampleRate:      the operating sample rate in Hz. Retained only so a
    //                   later sample-rate change is expressed as a second
    //                   prepare() call (see "Sample-rate changes" below);
    //                   S1 specifies delay length directly in samples, not
    //                   time, so sampleRate plays no other role here.
    // maxDelaySamples: the fixed delay length in samples for this
    //                   preparation. In S1 this is both the reserved
    //                   capacity and the active (only) delay length —
    //                   there is no separate runtime-settable length yet.
    //
    // Preconditions: sampleRate is finite and > 0.0; maxDelaySamples > 0.
    // On a precondition violation: returns false, performs no allocation,
    // and leaves the object exactly as it was before the call (if it was
    // already prepared, that prior preparation remains valid and usable;
    // if it was never prepared, it remains unprepared).
    // On success: allocates a zero-initialized buffer of exactly
    // maxDelaySamples samples, sets the object to the reset state (as
    // reset() defines below), and returns true. Not noexcept: allocation
    // failure (std::bad_alloc) propagates to the caller, which is
    // acceptable because this call is off the render thread.
    bool prepare(double sampleRate, std::size_t maxDelaySamples);

    // Zeros all delay-history storage and returns the internal write
    // position to its initial value, without deallocating or resizing.
    // Safe to call at any time, including before prepare() (a no-op then:
    // there is no storage to clear, and none is allocated). Guarantees:
    // immediately after reset(), processing a zero-valued input block of
    // any length up to maxDelaySamples yields an all-zero output block
    // (silence-in, silence-out; ADR-002 "the zero state is an
    // equilibrium"). Repeatable: any number of reset() calls, interleaved
    // with any process() calls, always restores this same state.
    void reset() noexcept;

    // Advances the delay line by count samples. For i in [0, count):
    // writes input[i] into the delay history at the current write
    // position, reads the sample exactly maxDelaySamples behind it (i.e.
    // the value written maxDelaySamples samples ago, or the initial zero
    // if fewer than maxDelaySamples samples have been written since the
    // last reset/prepare) into output[i], then advances the write
    // position by one sample, wrapping modulo maxDelaySamples.
    //
    // input and output are caller-owned and must each have storage for at
    // least count samples, exactly as GainProcessor's samples buffer is
    // caller-owned and sized by count. input and output may be the same
    // pointer (in-place use): each input sample is captured before the
    // corresponding output sample is written, so aliasing is safe.
    //
    // count == 0 is always a safe no-op: it returns immediately without
    // reading input, writing output, or touching internal state,
    // regardless of whether the object has been prepared. input/output
    // may be null only when count == 0 (matching GainProcessor's null
    // convention).
    //
    // Precondition for count > 0: prepare() must have returned true at
    // least once since construction (or since the last failed
    // prepare()/reset()-before-prepare sequence that left the object
    // unprepared). This is a caller contract, not a runtime-checked
    // sanitizer, matching GainProcessor's existing "small internal
    // contract, not a sanitizer for arbitrary malformed audio" stance.
    //
    // noexcept, allocation-free, lock-free, branch count independent of
    // sample values (per AETHERFIELD_SPEC.md §19).
    void process(const float* input, float* output, std::size_t count) noexcept;

    // Returns the value currently held at the write position, without
    // advancing or modifying any state. This is exactly the value
    // process() would return as its "delayed" output if called right now
    // with any input — peek() then push() is process()'s existing
    // single-sample behavior split into its two independent halves; see
    // push() below. Returns 0.0F if called before prepare() (there is no
    // storage to read; this matches the reset state's zero fill and
    // process()'s existing "or the initial zero" language).
    //
    // noexcept, allocation-free, branch count independent of sample
    // values. Enables composing several DelayLine instances into a
    // coupled network (docs/phase1-s2-plan.md) whose per-sample recurrence
    // requires reading every line's current output before deciding what to
    // write into any of them — something process() alone cannot express,
    // since it takes the value to write as an input to the very call that
    // reveals the value being evicted.
    float peek() const noexcept;

    // Writes value into the delay history at the current write position
    // and advances the write position by one sample, wrapping modulo
    // maxDelaySamples — exactly process()'s existing write-and-advance
    // half, with the read half removed. Does not read or return the value
    // being overwritten; call peek() first if that value is needed.
    // process() itself is unchanged by this addition and remains
    // equivalent to calling peek() then push(input[i]) for each sample in
    // its block, for i in [0, count).
    //
    // Precondition: prepare() must have returned true at least once since
    // construction — the same caller contract process() already
    // documents for count > 0, not a runtime-checked sanitizer.
    //
    // noexcept, allocation-free, branch count independent of sample
    // values.
    void push(float value) noexcept;

private:
    std::vector<float> storage_;      // size == 0 (unprepared) or maxDelaySamples
    std::size_t writePos_ = 0;        // invariant: writePos_ < storage_.size(), or 0 when unprepared
};

} // namespace aetherfield::dsp
