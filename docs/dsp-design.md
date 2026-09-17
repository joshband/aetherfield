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

## PLANNED: Phase 1 architecture beyond S1

The late-network topology, its damping filter, denormal mitigation, numerical bounds, full-network initialization/reset/sample-rate lifecycle, and the initial parameter surface (Mix, Decay, Damp — units, mappings, smoothing and transport) are now **decided but unimplemented** (ADR-002 through ADR-004, [decisions.md](decisions.md)). Modulation and stereo extraction remain hypotheses and are explicitly not authorized by any accepted ADR; ADR-004 records why the accepted parameter set is structurally incapable of expressing modulation. Terra implements further reverb components only under separate authorization; see [phase1-s1-plan.md](phase1-s1-plan.md) for the S1 increment already implemented above.

No feedback network, decay calculation, freeze state, Bloom, Texture, pitch or spectral processor exists. Therefore RT60, feedback stability, tail quality, stereo decorrelation and mobile CPU suitability have not been measured. `DelayLine` alone is not a reverb and makes no sonic claim.

Mix, Decay and Damp now have units, ranges, mappings, smoothing and automation contracts (ADR-004); their implementation is not authorized by that ADR alone. Any further continuous product parameter still requires the same before implementation. Future stateful DSP requires explicit preparation, reset, sample-rate and ownership contracts. Those interfaces should follow the accepted reverb design rather than the trivial gain demonstration.
