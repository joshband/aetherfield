# ADR-006 implementation-readiness review

**Historical review status: complete; corrections resolved-by-contract,
2026-09-17. Current completion note (2026-09-18):** DS-B Tasks 1–4 are now
implemented and measured, including the propagated C3 cessation bound and
DS-13. Three owner listening rounds and Sol review close the DS-B Sonic
acceptance component; no ADR-006 revision is warranted. This closure does not
authorize a product signal path, host wrapper, UI, modulation, Freeze, Bloom,
Texture, or a final product line count.
Scope: the uncommitted [ADR-006](../decisions/ADR-006-diffusion-stereo.md), checked against
`FeedbackDelayNetwork`, `ParameterAutomation`, `DelayLine`, and the existing
NS/PT plans. This review does not supersede the chosen topology or claim new
DSP evidence. The topology remains an evaluation baseline. Its former decay,
coherence, silence, detector and channel-balance claims are superseded by
ADR-006's correction note and the bounded
[DS-B integration plan](phase1-ds-integration-plan.md); implementation includes
Task 1 transactional lifecycle/preparation, Task 2a's read-only pre-step tap
view, Task 2b's one-sample route, and Task 3 aggregate fault/recovery path.
The remaining text preserves the 2026-09-17 readiness review and its then-open
authorization/evidence statements as historical context; it is not the current
status.

**Task 2a handoff (2026-09-17):** the additive FDN accessor returns current
even/odd `peek()` sums in increasing-delay line order and is forwarded through
the wrapper without advancing either object. The focused red build failed with
no `preStepTapSums` member; a fresh Release FDN/wrapper focus then passed 2/2
in 1.17 s and the full suite passed 6/6 in 1.82 s. It establishes only a
read-only pre-advance observation. Task 2b subsequently implemented the
one-sample wrapper call order and taps-to-output route; block partition
evidence remains unimplemented.

**Task 1 validation handoff (2026-09-17):** preparation now treats the
realized delay topology as a transactional feasibility gate. It rejects
non-increasing input/interleaved-output windows, all diffusion-to-diffusion
and diffusion-to-FDN GCD conflicts, and any diffusion length at or above the
realized FDN minimum delay. It also bounds every wrapper time-derived target
to `[1, INT_MAX - 1024]` samples before the unchanged FDN can call `llround`.
A focused red test previously exited 1 with `FAIL: invalid realized diffusion
topology accepted`; the fresh Release focused suite is now 1/1 in 0.01 s and
the full suite 6/6 in 1.88 s. This validates only Task 1 preparation and
rollback; it does not supply wrapper advance/order or later audio behavior.

## Findings that affect acceptance

### R1 — Full-path decay is not invariant under allpass insertion

ADR-006 (e), DS-4, and the Rationale say an allpass adds no modes and cannot
change measured decay. Its own denominator, `1 - g*z^-d`, has `d` poles.
At `g=(sqrt(5)-1)/2`, `d=479`, and 48 kHz, their amplitude decay time is
`-3*d/(fs*log10(g)) = 0.1432500983 s`. The existing network permits
`T60_min ≈ 0.006408 s`. A flat magnitude response does not remove these
transient dynamics. Output taps also change modal observability and can
cancel modes; unchanged network poles do not guarantee equal fitted decay.

**Required correction:** preserve the existing network-only NS-6 regression;
measure full-path decay separately, including minimum Decay and high Damp.
Do not require unconditional equality with the bare network. State that the
late network's poles are unchanged, while external allpasses add poles.
See [Smith, Allpass Filter Sections](https://www.dsprelated.com/freebooks/filters/Allpass_Filter_Sections.html)
for the allpass pole/zero structure; the counterexample above is our derivation.

### R2 — Coherence is not the same as broadband correlation

For the ideal fixed, linear mono-input path, write `L=H_L X`, `R=H_R X`.
Then `S_LL=|H_L|² S_XX`, `S_RR=|H_R|² S_XX`, and
`S_LR=H_L conj(H_R) S_XX`. Consequently magnitude-squared coherence
`|S_LR|²/(S_LL S_RR)` is **1 wherever both output spectra are nonzero**.
Different delays and allpasses can lower zero-lag correlation without lowering
this ideal coherence. Finite-window estimates, initialization and the cutoff
can depart from that ideal; none makes zero coherence an appropriate target.

**Required correction:** retain DS-7 as a diagnostic, state its estimator,
windows, overlap, excitation and silent-bin handling, and remove the implied
zero-coherence success criterion. Measure zero-lag and short-lag normalized
cross-correlation separately. An impulse's single-periodogram coherence is
not a useful width test. [MathWorks' coherence documentation](https://www.mathworks.com/help/signal/ref/mscohere.html)
defines the estimator and warns that a single segment returns unity.

### R3 — Exact-silence numbers are not a proved cascade bound

ADR-006 (h)(6) starts each section at unit state with no further input, then
adds those times. A downstream section is driven until its predecessor
stops, and its state need not then be bounded by one. Headroom is explicitly
greater than one elsewhere in the ADR. A discrete delay recurrence also
needs an integer circulation count and the actual stored float coefficient.

**Required correction:** for each section, bound state amplitude when its
input becomes zero, then derive a conservative integer drain bound. Propagate
those bounds through input chain, network and output chains. The quoted
1.699/0.993/1.244 s figures reproduce the unit-state arithmetic, but the
claimed network-bound-plus-2.9 s whole-path guarantee is not established.
Keep measured exact silence, reset and cutoff-placement tests; do not invent
a timeout that quietly replaces the proof.

### R4 — Detector ownership needs an explicit contract

The existing network owns a private saturating count/latch. `processSample()`
substitutes non-finite input but stores internally generated faults unchanged;
`reset()` clears its latch and preserves its count. It does not perform the
whole-path boundary reset ADR-006 requires. A new input diffuser's overflow
arrives at the network as an input and is therefore substituted there.

**Required correction:** specify a wrapper-owned aggregate detector, how it
observes network count increments without double counting, how diffusers
report internal faults before the network guard can obscure them, and when
the next nonempty block performs full reset. Preserve cumulative counts
across reset and re-prepare; specify saturation and zero-frame semantics.
Leave standalone FDN behavior unchanged. Existing fault branches also mean
constant instruction/branch counts cannot simply be claimed for exceptional
and normal inputs; scope and measure that requirement explicitly.

### R5 — Channel balance and the 3.01 dB result are conditional

Unit tap norms and disjoint support imply equal power and zero cross term
only under the stated equal-power, uncorrelated-line assumption. Neither
holds automatically for the shared mono drive. The actual fold-down power
is `P_L + P_R + 2*E[L R]`. Dry/wet mixing has its own covariance term.
The first left wet arrival is at `m_0`; the first right is at `m_1`, not the
same sample. Allpass direct terms preserve those different tap onsets.

**Required correction:** DS-8 records actual sum power and covariance;
3.01 dB is a conditional comparison, not a mandatory pass value. DS-9 must
separate tap onset, total energy centroid and output-diffuser contribution.
The 126-sample output-chain length difference is not a bound on whole-path
channel imbalance or centroid separation. Record those before adopting a
perceptual tolerance. No stereo-quality acceptance follows from tap algebra.

## HISTORICAL: 2026-09-17 resolution status — contract complete, evidence still pending

The following table preserves the review's status at that date. Its
"unmeasured" and "unimplemented" cells were closed by the later DS-B Task 4
and closure record described above and in testing.md.

| Finding | Sol resolution | Current status |
|---|---|---|
| R1 — full-path decay | External allpasses add poles; retain NS-6 only for the bare FDN and measure full-path decay separately. | Resolved by ADR-006 C1 and DS-B Task 4; unmeasured. |
| R2 — coherence | Ideal fixed mono-input magnitude-squared coherence is one at nonzero bins; use recorded Welch estimation with at least two segments and separate zero/short-lag correlation. | Resolved by ADR-006 C2 and DS-B Task 4; unmeasured. |
| R3 — silence | Use each stored float `q_j`, a proved cessation-state bound `S_j`, and integer `D_j=(k_j+1)d_j`; this is a proof template, not a timing claim. | Resolved by ADR-006 C3 and DS-B Task 4; no cascade proof or timing measurement exists. |
| R4 — detector | The wrapper aggregates diffuser events and FDN count/latch observations, preserves cumulative count, and performs the full reset at the next nonempty block. | Resolved by ADR-006 C4; DS-B Task 1 owns storage/lifecycle only, Task 3 observation/recovery is unimplemented. |
| R5 — channel metrics | Treat 3.01 dB as conditional; record covariance, actual sum power, first arrivals and full-path/isolated centroids. | Resolved by ADR-006 C5 and DS-B Task 4; unmeasured. |

The correction resolves the review's unsupported contracts without changing the
topology decision. It does not accept DS-B implementation, validate the
baseline, or close any full-chain DS case.

## Checks that support proceeding with a smaller increment

- Recomputed the default lengths at 48 kHz: input `{47,103,223,479}`;
  output `{191,241,307,383}`. At 44.1 kHz: input `{43,97,199,439}`;
  output `{173,223,281,353}`. These match ADR-006.
- Recomputed all eight `(K_in,K_out)` brackets at both rates: no duplicates
  across the two diffusion windows. This is arithmetic, not runtime evidence.
- The section recurrence, coefficient guard, fixed delays, and normalized
  uniform injection are suitable for a bounded primitive milestone.
- The existing network needs only a const pre-step even/odd tap accessor:
  sum `lines_[i].peek()` before calling `processSample()`. Reading its
  `scratch_` after processing would be wrong: it has been overwritten by
  damping, gain and the Hadamard transform.
- A fresh Release configure/build and CTest run on 2026-09-17 passed 4/4
  existing suites in `/private/tmp/aetherfield-resume-20260917`. This verifies
  the baseline only, not diffusion or stereo.

## Recommended sequence

1. Retain DS-A as the implemented standalone primitive; it does not close a
   full-chain case.
2. Use the ADR-006 correction note and
   [DS-B integration plan](phase1-ds-integration-plan.md) as the source of
   truth for any future implementation authorization.
3. Authorize at most DS-B Task 1 as a first implementation increment, then
   review its evidence before authorizing the tap/order or fault tasks.
4. Interpret DS-4/7/8/9/10 results only after full-chain measurements and a
   separate owner listening review.

The current work completes review correction and integration planning. It does
not claim that the entire diffusion/stereo subsystem is implemented, validated
or ready for unbounded implementation.
