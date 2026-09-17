# DSP design

## IMPLEMENTED: bootstrap gain only

The entire current DSP topology is scalar multiplication of a caller-owned channel buffer:

`y[n] = gain * x[n]`

The gain is constant for a block and passed by value. Processing is in place, stateless and `noexcept`; it contains no allocation, locks, logging, file I/O, blocking synchronization or UI work. There is no initialization or reset lifecycle because this processor has no state.

The caller supplies valid storage for the requested sample count. A null pointer is permitted only for a zero-length call. Inputs and gain must be finite, and their product must remain representable. This is a small internal contract, not a sanitizer for arbitrary malformed audio. Finite gain alone does not guarantee finite output for unbounded inputs.

The fixture's fixed gain is not a product parameter. Changing it discontinuously can click; smoothing and automated transitions are not implemented or claimed. There is no concurrent control-thread access in Phase 0.

## IMPLEMENTED: Phase 1 S1 — delay/lifecycle skeleton

`DelayLine` (`src/dsp/DelayLine.h`/`.cpp`) is a single fixed-length delay line: `y[n] = x[n - N]`, with `N` (`maxDelaySamples`) fixed for the lifetime of one `prepare()` call. It has no feedback, no matrix, no damping filter, and no runtime-adjustable length, pre-delay, modulation or interpolation — all of that remains PLANNED below and in ADR-002's S2 milestone.

Unlike the stateless gain function, `DelayLine` is a small stateful class: `prepare()` is the only allocation point (off the render thread), `reset()` zeros history without reallocating, and `process()` is `noexcept`, allocation-free and branch-count-independent of sample values. Invalid preparation (`sampleRate` non-finite or `<= 0.0`, `maxDelaySamples == 0`) is rejected without disturbing any prior valid preparation. See [phase1-s1-plan.md](phase1-s1-plan.md) for the full interface/lifecycle contract and [testing.md](testing.md) for implementation and independent verification evidence.

## IMPLEMENTED: Phase 1 S2 — fixed late network

`FeedbackDelayNetwork` (`src/dsp/FeedbackDelayNetwork.h`/`.cpp`) is the NS-1…NS-11 gate's fixed, time-invariant late network: `N ∈ {4, 8, 16}` coupled `DelayLine`s (ADR-005's bracket), one normalized-Hadamard feedback matrix applied via the O(N log N) fast transform with its `1/√N` normalization folded into each line's gain, and one bounded-real one-pole damping filter per line, in series inside the loop, exactly as ADR-002/ADR-003 decided. Delay lengths are derived internally at `prepare()` from a time-domain specification via ADR-005's nearest-prime rule — never accepted as a sample count. ADR-003 (b)'s deterministic denormal cutoff (`ε = 1e-20`) is applied at exactly its two named recursive memories; a non-finite value is counted and latched, never silently corrected. No modulation, diffusion, stereo strategy, or parameter-transport/automation layer exists. See [phase1-s2-plan.md](phase1-s2-plan.md) for the full interface/lifecycle contract and [testing.md](testing.md) for implementation and independent verification evidence.

`DelayLine` also gained two small non-breaking additions during S2's implementation, `peek()` and `push()`, splitting `process()`'s existing single-sample behavior into its two independent halves so several lines can be composed into a coupled network; `process()` itself, and all 9 of its original S1 tests, are unchanged.

## PLANNED: Phase 1 architecture beyond S2

The initial parameter surface (Mix, Decay, Damp — units, mappings, smoothing and transport) is decided but unimplemented (ADR-004, [decisions.md](decisions.md)); its parameter-transport class and the PT-1…PT-9 gate are a separate, still-unplanned future step. The product's final `N`, the supported sample-rate matrix, and `T60_min`/`T60_max`/`D_max` stay **DEFERRED** (ADR-005). Modulation and stereo extraction remain hypotheses and are explicitly not authorized by any accepted ADR; ADR-004 records why the accepted parameter set is structurally incapable of expressing modulation. Terra implements further reverb components only under separate authorization.

No decay-calculation product parameter, freeze state, Bloom, Texture, pitch or spectral processor exists, and no input/output diffusion or stereo strategy exists — `FeedbackDelayNetwork`'s NS-test injection/tap convention is explicitly a test-only convention, not a product decision (phase1-s2-plan.md). Therefore feedback stability under real product parameters, tail quality, stereo decorrelation and mobile CPU suitability have not been measured. `FeedbackDelayNetwork` alone is not a reverb and makes no sonic claim.

Mix, Decay and Damp now have units, ranges, mappings, smoothing and automation contracts (ADR-004); their implementation is not authorized by that ADR alone. Any further continuous product parameter still requires the same before implementation. Future stateful DSP requires explicit preparation, reset, sample-rate and ownership contracts. Those interfaces should follow the accepted reverb design rather than the trivial gain demonstration.
