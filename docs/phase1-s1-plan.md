# Phase 1 S1 — Delay and lifecycle skeleton: implementation task plan

This is the deliverable for roadmap.md Phase 1 row 4 ("Translate accepted design
into small implementation increments"), covering only ADR-002's S1 milestone
("Delay and lifecycle skeleton", decisions.md). It targets exactly
testing.md's PLANNED gate 1 ("Delay/lifecycle skeleton"). It does not cover
S2 (the fixed late network: multiple lines, the feedback matrix, per-line
damping) — S2 is a separate future plan, authorized separately.

## Non-goals (read first)

This document is a design/task plan only, per roadmap row 4's acceptance
bar: "implementation reserved for later authorization." It does **not**
authorize:

- Writing or editing any `.h`/`.cpp`/`CMakeLists.txt` file.
- A feedback matrix, multiple delay lines, a damping filter, or any
  reverb/decay behavior. ADR-002 S1 is explicitly "one fixed-length delay
  line, no feedback, no matrix."
- A runtime-adjustable delay length ("Size"), pre-delay, modulation, or
  interpolation. Those belong to Phase 1 row 3 (parameter semantics) and are
  out of scope here. In this design, the delay length is fixed for the
  lifetime of a given preparation.
- Denormal/NaN/Inf mitigation design. This primitive contains no feedback
  path (ADR-002 §"Numerical safety" concerns arise from feedback loops); a
  sample written into the delay line is read back unmodified, so no new
  decay, scaling or accumulation occurs here. Only finite-input handling
  (inherited from the caller contract, as in GainProcessor) applies.

Implementation of the interface below is a future, separately authorized
step.

## Interface design rationale: stateful class, not a free function

GainProcessor (`src/dsp/GainProcessor.h`/`.cpp`) is a stateless free function
because `y[n] = gain * x[n]` carries no memory between calls: the caller owns
the one buffer involved (the signal itself), and there is nothing else to
own or persist.

A delay line is different in kind, not degree: correct operation requires
history that outlives any single `process()` call — the last (up to)
`maxDelaySamples` input values must persist between calls in order to be
read back later. That history is DSP *state*, not the audio *signal buffer*.
A free function taking `(samples, count)` has nowhere to keep it. The
precedent this plan follows is therefore a small stateful class, matching
the pattern dsp-design.md already anticipates ("Future stateful DSP requires
explicit preparation, reset, sample-rate and ownership contracts") and the
render-lifecycle shape ADR-001/ADR-002 assume (prepare once off the render
thread; process and reset repeatedly on it).

Ownership split, by analogy with GainProcessor:

- **Signal buffers** (`process()`'s `input`/`output`): caller-owned, exactly
  as GainProcessor's `samples` buffer is caller-owned. The class never
  allocates, frees, or retains a pointer to them past the call.
- **Delay history** (the ring buffer): owned internally by the `DelayLine`
  object itself, because it must survive across calls in a way no per-call
  signal buffer does. It is allocated in `prepare()` (see below) and freed
  by the object's destructor. The caller never sees or sizes this storage
  directly.

## Exact interface

File location for a future implementation (not created by this plan):
`src/dsp/DelayLine.h` / `src/dsp/DelayLine.cpp`, `namespace aetherfield::dsp`,
alongside `GainProcessor.h`/`.cpp`.

```cpp
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

private:
    std::vector<float> storage_;      // size == 0 (unprepared) or maxDelaySamples
    std::size_t writePos_ = 0;        // invariant: writePos_ < storage_.size(), or 0 when unprepared
};

} // namespace aetherfield::dsp
```

### Preparation/lifecycle contract (summary)

- **One allocation point.** `prepare()` is the only place `DelayLine`
  allocates (e.g. via `std::vector<float>::assign`/`resize`). The
  constructor, `reset()`, and `process()` never allocate.
- **Invalid preparation is rejected, not clamped or silently accepted.**
  `sampleRate <= 0.0`, non-finite `sampleRate`, or `maxDelaySamples == 0`
  each cause `prepare()` to return `false` with no allocation and no change
  to any prior valid preparation.
- **Reset guarantees.** Zeroed storage, write position reset to its initial
  value, silence-in/silence-out, repeatable regardless of call history.
- **Sample-rate changes are not a separate API.** ADR-002 requires a rate
  change to invalidate all retained state and imply a full reset. This
  interface expresses that by having no `setSampleRate()`: a rate change is
  performed by calling `prepare()` again with the new `sampleRate` and an
  appropriate `maxDelaySamples` for that rate. Every successful `prepare()`
  call — first or repeated — unconditionally reallocates storage sized to
  the given `maxDelaySamples` and performs a full reset, so there is no
  stale state from a previous rate or length to reason about.

### Process contract: exact impulse position and wraparound

`process()` is **block-based** (`(input, output, count)`), not
single-sample. Justification: it matches `GainProcessor`'s existing call
convention (caller-owned buffers, explicit `count`), matches the render
callback's natural per-block invocation under AETHERFIELD_SPEC.md §19, and a
single-sample call is just `count == 1`, so nothing is lost.

The implementation is the classic single-buffer, length-`N` delay line: read
the sample at the write position *before* overwriting it. With
`N = maxDelaySamples` and history zero-initialized at reset, this produces
exactly `y[n] = x[n - N]` (zero for `n < N`), because the slot about to be
overwritten at global sample `n` was last written at global sample `n - N`
(the write position cycles through all `N` slots exactly once per `N`
samples).

**"Exact impulse position" verification, concretely:** prepare with a known
`maxDelaySamples = N` (e.g. `N = 5`); process a block containing a single
unit impulse at input index `k` (all other samples zero); assert the output
is zero at every index except `k + N`, where it equals the impulse value.

**"Wraparound" verification, concretely:** process a longer run (at least
`3N + 1` samples) using block sizes that do **not** evenly divide `N`, so
the internal write position wraps mid-block one or more times. Place
impulses at several input indices spanning multiple wraps (e.g. `0`, `N`,
`2N + 1`). Assert each reappears at exactly `input index + N` in the output,
regardless of block boundaries, and nowhere else — this is what
distinguishes "the buffer index arithmetic wraps correctly" from "it happens
to work only when a block equals `N`."

**Buffer extents:** `process()` must read/write exactly `count` samples of
`input`/`output` and no more — verified by placing sentinel values just past
the requested extent (as `GainProcessorTests.cpp` already does for
`GainProcessor`) and asserting they are untouched.

**Zero-frame calls:** `process(input, output, 0)` — including with null
`input`/`output` — must be safe and a strict no-op, both before and after a
successful `prepare()`.

## Test plan

Future file: `tests/DelayLineTests.cpp`, `int main()`, no external test
framework — named cases, explicit failure returns, nonzero exit on failure,
in the exact style of `tests/GainProcessorTests.cpp`'s `fail(const char*)`
helper. Each case below maps 1:1 to a testing.md gate-1 bullet.

| # | Named case | testing.md gate-1 bullet | What it asserts |
|---|---|---|---|
| 1 | `testExactImpulsePosition` | Exact impulse positions | `prepare(48000.0, 5)`; impulse at input index 0; output is 0 everywhere except index 5, where it equals the impulse. |
| 2 | `testWraparoundAcrossMultipleCycles` | ...and wraparound | `prepare(48000.0, 5)`; run ≥16 samples across non-`N`-dividing block sizes (e.g. blocks of 3, 4, 3, 6); impulses at indices 0, 5, 11; each reappears only at `index + 5`. |
| 3 | `testPrepareRejectsZeroMaxDelay` | Rejected invalid preparation | `prepare(48000.0, 0)` returns `false`. |
| 4 | `testPrepareRejectsNonPositiveOrNonFiniteSampleRate` | Rejected invalid preparation | `prepare(0.0, 64)`, `prepare(-48000.0, 64)`, and `prepare(NAN, 64)` each return `false`. |
| 5 | `testInvalidReprepareDoesNotDisturbPriorState` | Rejected invalid preparation | After a valid `prepare(48000.0, 5)` and one processed impulse, an invalid `prepare(48000.0, 0)` returns `false`, and the object still behaves per its original 5-sample preparation (same impulse-position check as case 1). |
| 6 | `testResetIsRepeatable` | Repeatable reset | Process non-zero input, call `reset()`, process a zero-valued block and confirm all-zero output (silence-in/silence-out); repeat the whole sequence a second time and confirm byte-identical results to the first. |
| 7 | `testBufferExtentsRespected` | All buffer extents respected | Sentinel values placed immediately past `count` in both `input` and `output` arrays are unchanged after `process()`, for `count` both smaller and larger than `maxDelaySamples`. |
| 8 | `testZeroFrameCallSafe` | Zero-frame calls safe | `process(nullptr, nullptr, 0)` before `prepare()`, and `process(nullptr, nullptr, 0)` after `prepare()`, both return without side effects (verified by an unaffected subsequent impulse-position check). |
| 9 | `testAllocationOccursOnlyDuringPreparation` | Allocation occurs only during preparation | Global `operator new`/`operator delete` are overridden in the test binary with a call counter (test-only instrumentation, not part of `DelayLine`). Assert zero allocations across construction, `reset()`, and a multi-block `process()` run of several thousand samples; assert allocation count increases only across `prepare()` calls (including a re-`prepare()` for a simulated sample-rate change). |

Test-binary allocation instrumentation for case 9 (illustrative sketch, not
authorized source — the actual file is written when S1 implementation is
authorized):

```cpp
namespace {
std::size_t allocationCount = 0;
}

void* operator new(std::size_t size) {
    ++allocationCount;
    return std::malloc(size);
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
```

## Build wiring (future, not applied now)

- Add `src/dsp/DelayLine.cpp` to the existing `aetherfield_dsp` STATIC
  library sources in `CMakeLists.txt`.
- Add a second test executable, mirroring the existing single-executable
  pattern (`aetherfield_dsp_tests` built from one `main()` in
  `GainProcessorTests.cpp`) rather than merging two `main()` functions:
  `add_executable(aetherfield_dsp_delay_tests tests/DelayLineTests.cpp)`,
  linked against `aetherfield_dsp`, with the same
  `-Wall -Wextra -Wpedantic -Werror` options applied to the other targets,
  and registered as its own CTest case:
  `add_test(NAME aetherfield_dsp_delay_tests COMMAND aetherfield_dsp_delay_tests)`.

None of the above is applied by this document. It is the exact shape a
future, separately authorized implementation step should follow.
