# DSP design

## IMPLEMENTED: bootstrap gain only

The entire current DSP topology is scalar multiplication of a caller-owned channel buffer:

`y[n] = gain * x[n]`

The gain is constant for a block and passed by value. Processing is in place, stateless and `noexcept`; it contains no allocation, locks, logging, file I/O, blocking synchronization or UI work. There is no initialization or reset lifecycle because this processor has no state.

The caller supplies valid storage for the requested sample count. A null pointer is permitted only for a zero-length call. Inputs and gain must be finite, and their product must remain representable. This is a small internal contract, not a sanitizer for arbitrary malformed audio. Finite gain alone does not guarantee finite output for unbounded inputs.

The fixture's fixed gain is not a product parameter. Changing it discontinuously can click; smoothing and automated transitions are not implemented or claimed. There is no concurrent control-thread access in Phase 0.

## PLANNED: Phase 1 architecture

The late-network topology, its damping filter, denormal mitigation, numerical bounds, and full-network initialization/reset/sample-rate lifecycle are now **decided but unimplemented** (ADR-002 and ADR-003, [decisions.md](decisions.md)). Modulation, stereo extraction, and parameter semantics remain hypotheses and are explicitly not authorized by either ADR. Terra implements reverb components only under separate authorization; see [phase1-s1-plan.md](phase1-s1-plan.md) for the first bounded increment.

No feedback network, decay calculation, freeze state, Bloom, Texture, pitch or spectral processor exists. Therefore RT60, feedback stability, tail quality, stereo decorrelation and mobile CPU suitability have not been measured. Phase 0 proves an engineering loop, not sonic quality.

Future continuous product parameters require units, ranges, mappings, smoothing, automation behavior and extreme-value tests. Future stateful DSP requires explicit preparation, reset, sample-rate and ownership contracts. Those interfaces should follow the accepted reverb design rather than the trivial gain demonstration.
