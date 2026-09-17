# DSP design

## IMPLEMENTED: Phase 0 bootstrap gain

The bootstrap gain processor multiplies a caller-owned channel buffer:

`y[n] = gain * x[n]`

The gain is constant for a block and passed by value. Processing is in place, stateless and `noexcept`; it contains no allocation, locks, logging, file I/O, blocking synchronization or UI work. There is no initialization or reset lifecycle because this processor has no state.

The caller supplies valid storage for the requested sample count. A null pointer is permitted only for a zero-length call. Inputs and gain must be finite, and their product must remain representable. This is a small internal contract, not a sanitizer for arbitrary malformed audio. Finite gain alone does not guarantee finite output for unbounded inputs.

The fixture's fixed gain is not a product parameter. Changing it discontinuously can click; smoothing and automated transitions are not implemented or claimed. There is no concurrent control-thread access in Phase 0.

## IMPLEMENTED: Phase 1 S1 — delay/lifecycle skeleton

`DelayLine` (`src/dsp/DelayLine.h`/`.cpp`) is a single fixed-length delay line: `y[n] = x[n - N]`, with `N` (`maxDelaySamples`) fixed for the lifetime of one `prepare()` call. It has no feedback, no matrix, no damping filter, and no runtime-adjustable length, pre-delay, modulation or interpolation — those capabilities belong to other components or remain deferred; the implemented fixed network is described below.

Unlike the stateless gain function, `DelayLine` is a small stateful class: `prepare()` is the only allocation point (off the render thread), `reset()` zeros history without reallocating, and `process()` is `noexcept`, allocation-free and branch-count-independent of sample values. Invalid preparation (`sampleRate` non-finite or `<= 0.0`, `maxDelaySamples == 0`) is rejected without disturbing any prior valid preparation. See [phase1-s1-plan.md](phases/phase1-s1-plan.md) for the full interface/lifecycle contract and [testing.md](testing.md) for implementation and independent verification evidence.

## IMPLEMENTED: Phase 1 S2 — fixed late network

`FeedbackDelayNetwork` (`src/dsp/FeedbackDelayNetwork.h`/`.cpp`) is the NS-1…NS-11 gate's fixed, time-invariant late network: `N ∈ {4, 8, 16}` coupled `DelayLine`s (ADR-005's bracket), one normalized-Hadamard feedback matrix applied via the O(N log N) fast transform with its `1/√N` normalization folded into each line's gain, and one bounded-real one-pole damping filter per line, in series inside the loop, exactly as ADR-002/ADR-003 decided. Delay lengths are derived internally at `prepare()` from a time-domain specification via ADR-005's nearest-prime rule — never accepted as a sample count. ADR-003 (b)'s deterministic denormal cutoff (`ε = 1e-20`) is applied at exactly its two named recursive memories; a non-finite value is counted and latched, never silently corrected. The network itself contains no modulation, diffusion, stereo extraction or parameter transport; the separate implemented automation layer is described below. See [phase1-s2-plan.md](phases/phase1-s2-plan.md) for the full interface/lifecycle contract and [testing.md](testing.md) for implementation and independent verification evidence.

`DelayLine` also gained two small non-breaking additions during S2's implementation, `peek()` and `push()`, splitting `process()`'s existing single-sample behavior into its two independent halves so several lines can be composed into a coupled network; `process()` itself, and all 9 of its original S1 tests, are unchanged.

## IMPLEMENTED: Phase 1 parameter transitions

`ParameterAutomation` (`src/dsp/ParameterAutomation.h`/`.cpp`) is the PT-1…PT-9 gate's automation layer for Aetherfield's three accepted controls — Mix, Decay, Damp — exactly as ADR-004 (b)-(d) decided: double-precision coefficient derivation with bit-exact endpoint assignment (never evaluated through a formula that could round off-target), a single-writer/single-reader lock-free atomic transport with a generation counter, and render-thread linear-ramp smoothing (`τ_ramp = 20ms`) over the full `2N+2` coefficient set. It automates an already-prepared `FeedbackDelayNetwork` via three small additive methods (`processSample()`, `setLineGain()`, `setDampingCoefficientC()`) added to it for this purpose; S2's own 11 tests and `process(count)` contract are unchanged. `T60_min`/`T60_max` are computed from the ADR-005 fixture's own delay set; `D_max` uses an explicit, labelled test-fixture value (48 dB) rather than a product/perceptual one. See [phase1-pt-plan.md](phases/phase1-pt-plan.md) for the full interface and [testing.md](testing.md) for implementation and independent verification evidence, including two real test-design bugs caught and fixed during implementation (a vacuous-comparison window shorter than the network's own shortest delay line, in two separate tests).

No modulation exists or is authorized. Mix, Decay and Damp are automatable within their accepted ranges; Size, pre-delay, Mod Depth/Rate and every other charter §14 candidate remain deferred exactly as ADR-004 left them.

## IMPLEMENTED: Phase 1 DS-A — fixed Schroeder allpass primitive

`SchroederAllpass` (`src/dsp/SchroederAllpass.h`/`.cpp`) is a standalone,
fixed-delay section for the future diffusion path. `prepare()` derives a
nearest-prime delay from a time-domain request, allocates a candidate
`DelayLine`, and commits only after validation. `processSample()` implements
`v = x + g·s`, `y = s − g·v`, applies the existing `1e-20` memory cutoff, and
reports non-finite input/state/output through its return value without
substitution or autonomous reset. It does not implement a cascade, stereo,
FDN integration, automation, or a product control. The focused and full CTest
evidence is recorded in [testing.md](testing.md).

## PLANNED: Phase 1 architecture beyond parameter transitions

The product's final `N`, the supported sample-rate matrix, and `D_max`'s actual perceptual/product value stay **DEFERRED** (ADR-005, ADR-004; `D_max_fixture = 48dB` above is test-only, not a product decision). The input/output diffusion topology, the injection vector, the output tap design and the stereo decorrelation strategy are now **decided but unimplemented** (ADR-006); a Diffusion or Width control, pre-delay, wet tone and modulation remain hypotheses, explicitly not authorized by any accepted ADR. Terra implements further reverb components only under separate authorization.

No input/output diffusion or stereo strategy exists — `FeedbackDelayNetwork`'s NS-test injection/tap convention is explicitly a test-only convention, not a product decision (phases/phase1-s2-plan.md); ADR-006 decides the design that replaces it and authorizes no implementation of it. No freeze state, Bloom, Texture, pitch or spectral processor exists. Therefore feedback stability under real product usage, tail quality, stereo decorrelation and mobile CPU suitability have not been measured. Neither `FeedbackDelayNetwork` nor `ParameterAutomation` makes any sonic claim; audition remains testing.md's Sonic acceptance gate's job.
