# Phase 1 DS-B — diffusion/stereo evaluation-baseline integration plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one mono-input/stereo-output wrapper around the accepted
evaluation-baseline diffusion, FDN, taps and Mix path with corrected numerical
and measurement contracts.

**Architecture:** `DiffusionStereoPath` owns the input allpass cascade, the
existing `FeedbackDelayNetwork`, `ParameterAutomation`, two output allpass
cascades and an aggregate fault/recovery state. The FDN remains the existing
fixed, unmodulated network; it receives one normalized diffused input. A new
const accessor exposes pre-step even/odd line sums without changing FDN
processing. The wrapper owns whole-path lifecycle and records results for
evaluation rather than claiming a completed product reverb.

**Tech Stack:** dependency-free C++20, existing CMake/CTest host tests,
test-local double analysis and FFT/Welch helpers.

**Spec:** [ADR-006](../decisions/ADR-006-diffusion-stereo.md), especially its
2026-09-17 correction note, and the [readiness review](phase1-ds-review.md).

## Status and authorization

**Tasks 1-3 are implemented; Task 4 is partially measured, 2026-09-17.**
Task 1's transactional preparation, Task 2a's read-only tap accessor, Task
2b's one-sample audio routing, and Task 3's aggregate detector/block recovery
are implemented and independently reviewed. Task 4 (split into sub-tasks 4a
and 4b, plus a bracket-completion pass) records DS-1..12 evidence. The
bracket-wide independent-recurrence, energy, determinism and allocation
coverage and the per-Mix RMS/arrival/centroid coverage are now closed; the
propagated whole-chain DS-10 cessation-bound requirement below remains open
(see that bullet for why it was deliberately not attempted in the same
pass). ADR-006 remains an accepted
architecture and evaluation baseline: Task 4 measures the fixed, unmodulated
path decided there; it adds no product controls or modulation, and its
measurements do not establish sonic acceptance (see roadmap/Sonic acceptance
gate). Task 3 added only the documented block boundary and fault/recovery
behavior and did not alter standalone FDN semantics; Task 4 does not alter it
either.

## Global constraints

- Preserve `FeedbackDelayNetwork::process()` and `processSample()` behavior and
  all NS/PT regressions bit-for-bit except for a new read-only pre-step accessor.
- Fixed prepare-time delay lengths only; no modulation, interpolation, Diffusion
  control, Width control, pre-delay, wet tone, UI, renderer, host wrapper or
  product sample-rate expansion.
- Use `g_ap=(sqrt(5)-1)/2` and the ADR-006 time-domain delay targets. Validate
  all preparation inputs before replacing live state; allocations occur only in
  preparation; processing/reset remain `noexcept` and allocation-free.
- Apply `epsilon_float=1e-20F` only to recursive-memory writes. A fault is
  reported and recovered at the wrapper boundary; no stage silently hides it.
- Preserve cumulative aggregate fault counts across reset and re-prepare. A
  zero-frame call neither processes audio nor consumes a pending reset.
- Treat full-path decay, silence, coherence, channel balance, mono compatibility,
  CPU and perceptual results as measurements with stated fixtures, never as
  inherited allpass/tap algebra.

## Files and proposed interfaces

| File | Responsibility |
|---|---|
| `src/dsp/FeedbackDelayNetwork.h/.cpp` | Add an additive, const, pre-step even/odd tap accessor; leave FDN advance unchanged. |
| `src/dsp/DiffusionStereoPath.h/.cpp` | Own lifecycle, sample ordering, diffusion stages, automation, aggregate detector and block-boundary reset. |
| `tests/DiffusionStereoPathTests.cpp` | TDD coverage for preparation, ordering, fault recovery, determinism and corrected DS-1…DS-12 measurements. |
| `CMakeLists.txt` | Register the new target with the existing C++20/warning/CTest pattern. |
| `docs/testing.md` | Record actual commands, outputs, measured values and unresolved evaluation findings only after runs. |

Proposed public boundary, subject to the task's review before source exists:

```cpp
namespace aetherfield::dsp {
struct StereoSample { float left; float right; bool nonFinite; };
struct PreStepTapSums { float even; float odd; };
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

class DiffusionStereoPath {
public:
    bool prepare(const DiffusionStereoConfig& config); // transactional, off render thread
    void process(const float* mono, float* left, float* right, std::size_t count) noexcept;
    StereoSample processSample(float mono) noexcept;
    void reset() noexcept;
    std::size_t nonFiniteCount() const noexcept;
    bool nonFiniteLatched() const noexcept;
};
}
```

`DiffusionStereoConfig` carries only existing fixture values: sample rate, FDN
preparation values, `dMaxDb`, fixed input/output allpass time targets and the
fixed coefficient. `prepare()` accepts `allpassCoefficient` only when it is
the decided `g_ap=(sqrt(5)-1)/2` after float storage; it is present for one
transactional validation path, not as a control. The config must not accept
direct sample counts, dynamic stage counts or runtime diffusion/width controls.
Task 1 uses this one definition in all preparation tests rather than copying
arguments.

## Required sample order

For each nonempty input sample, the future wrapper must perform this exact
sequence. It is a contract, not pseudocode that an implementer may reorder:

1. At block entry, if `resetPending`, run the full reset in Task 3 before the
   first sample. For `count == 0`, return before this step.
2. Call `ParameterAutomation::checkForNewTargets()` once at the documented
   block boundary, then call `advance(fdn)` exactly once for this sample.
3. If the host input is non-finite, record the wrapper input event and replace
   only that injection value with `0.0F` before the first input allpass.
4. Process the input cascade in order. Observe every `Sample::nonFinite` before
   passing its value downstream; record one source observation per stage.
5. Multiply the finite diffused input by `1/sqrt(N)`. Read
   `fdn.preStepTapSums()` before `fdn.processSample()` advances any line,
   obtaining and retaining even/odd sums from that same line state.
6. Snapshot the FDN count/latch immediately before and after calling
   `fdn.processSample()` with the normalized diffused input to update the
   wrapper aggregate under Task 3.
7. Scale the retained even/odd sums by `1/sqrt(N/2)`, then process each through
   its assigned output allpass pair.
8. Apply this sample's returned dry/wet gains after output diffusion:
   `left = dry*input + wet*wetLeft`, `right = dry*input + wet*wetRight`.
9. Return the computed samples and whether the wrapper observed any fault on
   this sample. A fault only schedules the next nonempty-block reset; it never
   changes the current sample by an undocumented autonomous reset.

Task 2 must prove the accessor reads values before FDN advancement. Reading
`scratch_`, the old unweighted summed FDN output, or any post-advance state is
not an alternative implementation.

## Task 1 — transactional wrapper ownership and preparation

**Files:** create `src/dsp/DiffusionStereoPath.h/.cpp`; modify
`CMakeLists.txt`; create `tests/DiffusionStereoPathTests.cpp`.

**Consumes:** `SchroederAllpass::prepare/processSample/reset`, FDN preparation,
and `ParameterAutomation::prepare/reset`.

**Produces:** a one-owner wrapper and `Config` whose successful preparation
creates all component state before replacing live state.

- [x] Write failing tests for unprepared lifecycle behavior, valid
  48 kHz and 44.1 kHz preparation, and invalid config rollback. Assert the
  former delay lengths, coefficients and aggregate-fault state remain usable
  after each rejected rate/time/coefficient/FDN/automation input. The
  2026-09-17 validation follow-up additionally proves rejection and rollback
  for duplicate input times, non-increasing interleaved output times,
  diffusion/FDN GCD conflicts, `max(diffusion) >= FDN m_min`, and an enormous
  finite FDN time target before candidate allocation. Audio and zero-frame
  processing are intentionally deferred with Task 2's process API.
- [x] Run the focused target and confirm it fails because the wrapper/interface
  does not exist, not because a legacy suite is unavailable.
- [x] Implement `Config` validation before allocation. It rejects every
  time-derived sample target outside `[1, INT_MAX - 1024]`, including both FDN
  endpoints, before forwarding to the FDN's internal `llround` derivation.
  After preparing candidates, it requires strictly increasing input and
  interleaved output windows, pairwise co-prime realized diffusion lengths,
  co-primality with every realized FDN length, and
  `max(diffusion) < FDN m_min`. Prepare candidates for every input/output
  section, FDN and automation; move all candidates into live state only after
  every check succeeds. Allocation failure propagates and must leave prior live
  state intact.
- [x] On success, derive all ADR-006 times at the configured rate and establish
  the reset state: zeroed allpass/FDN/automation states, clear wrapper latch,
  preserve wrapper cumulative count, and `resetPending=false`.
- [x] Run focused preparation tests and the five existing suites. Do not add a
  renderer, host API, tap accessor or audio process API in this task.

## Task 2 — additive FDN pre-step taps and exact path ordering

**Files:** modify `src/dsp/FeedbackDelayNetwork.h/.cpp` and
`src/dsp/DiffusionStereoPath.cpp`; test
`tests/FdnTests.cpp` and `tests/DiffusionStereoPathTests.cpp`.

**Consumes:** the prepared wrapper from Task 1 and the FDN's increasing-delay
line order.

**Produces:** `PreStepTapSums FeedbackDelayNetwork::preStepTapSums() const
noexcept` and a wrapper forwarding view. The sample sequence above remains a
contract; its wrapper processing implementation is deferred.

- [x] Write a failing FDN characterization that prepares a small fixture,
  advances to a known line state, and verifies `preStepTapSums().even/.odd`
  equal the known pre-advance line-state sums before the next FDN call. The
  implemented test observes `{1,0}` after an impulse and `m_min - 1` zeros,
  verifies repeated const reads do not mutate it, and confirms that same state
  is the next FDN wet sample. The wrapper test proves unprepared and prepared
  reset forwarding is silent and non-mutating; no wrapper advance path exists
  yet to test post-advance ordering.
- [x] Run the focused FDN/wrapper build and confirm the missing accessor causes
  the expected compilation failure.
- [x] Implement the accessor as a const loop over existing lines in increasing
  delay order, adding even and odd `peek()` values without writes, allocation,
  scratch mutation or coefficient changes. Do not expose line storage. Expose
  the same read-only result through `DiffusionStereoPath` without adding a
  wrapper processing method.
- [x] Implement `processSample()` in the nine-step order above. Call
  `advance(fdn)` once per sample; never call it separately for left/right.
  Use the stored FDN line count for both normalization factors. The 2026-09-17
  implementation returns silence before preparation, sanitizes only a
  non-finite host injection, uses pre-advance even/odd taps, routes them
  through their respective output pairs, and applies the returned Mix gains.
  It reports direct stage observations per sample but leaves cumulative
  accounting, latch transitions and reset scheduling for Task 3.
- [x] Re-run NS-1…NS-11 unchanged and the focused FDN/wrapper tests. The
  fresh Release FDN/wrapper focus passed 2/2 in 1.17 s and the full suite 6/6
  in 1.82 s on 2026-09-17.
- [x] Run wrapper ordering and partition tests only after the separately
  authorized wrapper process API exists.
  The implemented block API is bit-identical across `{1,13,64,512,3}` fixed
  parameter partitions; automation-specific partition behavior remains
  governed by PT-5.

## Task 3 — aggregate detector and block-boundary recovery

**Files:** modify `src/dsp/DiffusionStereoPath.h/.cpp`; test
`tests/DiffusionStereoPathTests.cpp`.

**Consumes:** `SchroederAllpass::Sample::nonFinite`, FDN count/latch accessors,
and the Task 2 call order.

**Produces:** wrapper-level `nonFiniteCount()`, `nonFiniteLatched()` and
`resetPending` behavior without altering standalone FDN semantics.

- [x] Write failing tests for an input-allpass overflow, NaN at the wrapper
  head, an FDN-originated latch/count event, repeated reset, a zero-frame call
  while reset is pending, and the next nonempty block. Assert the current
  faulting block is not reset mid-sample; the following nonempty block begins
  from exact whole-path silence while cumulative count survives.
- [x] Add a saturation characterizer that drives the FDN count to
  `size_t` maximum through a test seam or bounded synthetic accessor fixture.
  Verify count delta `0` plus a false-to-true FDN latch still creates one
  aggregate latch-only observation, labelled non-exact rather than as a known
  number of faults.
- [x] Implement per-stage observation before downstream propagation. Snapshot
  FDN `(count,latch)` around its call; aggregate only a positive count delta,
  then independently observe a latch transition. Track source identity for the
  current sample so a known upstream event is not counted again solely because
  FDN input substitution observes the same value.
- [x] Implement `resetPending`: zero-frame processing returns immediately;
  the next call with `count>0` resets every input/output allpass, FDN and
  automation, clears current latches, and preserves cumulative counts before
  rendering its first sample. Re-prepare performs the same state reset without
  clearing cumulative count.
- [x] Re-run fault, reset, allocation and all legacy suites. Record observed
  fault-source/latch behavior; do not claim constant branches for exceptional
  input without a separate measured contract.

## Task 4 — corrected DS measurements and tests

**Files:** modify `tests/DiffusionStereoPathTests.cpp`; optionally create
`tests/DiffusionStereoAnalysis.h` for test-only radix-2 FFT, double recurrence
and Welch helpers; modify `docs/testing.md` after actual runs.

**Consumes:** the full fixed baseline from Tasks 1–3.

**Produces:** measured DS-1…DS-12 evidence with each prior overclaim removed.

- [x] Start with failing anti-vacuity checks: fixtures must contain nonzero wet
  samples before a decay/correlation/centroid result is accepted; test helpers
  must reject a single-segment coherence estimate.
- [x] DS-1/2/3/5/6/11/12: extend DS-A's independent recurrence, FFT, energy,
  adversarial peak, deterministic partition and allocation tests across every
  selected input/output cascade, both rates and every runnable
  `(K_in,K_out)` bracket. Check all time-derived lengths, increasing guards and
  direct gcds against FDN lengths. Record amplitude-aware density separately
  from lattice counts and report cost rather than using it as a budget.
  FFT, magnitude, peak, direct-gcd, runnable bracket and an amplitude-threshold
  density are present; a second, independently implemented double-precision
  recurrence reference (`testDs2IndependentDoubleRecurrenceAcrossBracket`,
  not sharing code with `SchroederAllpass` or the radix2Fft-based magnitude
  check, max observed error ~2e-7 against a 1e-5 tolerance) and
  energy/determinism/allocation across every bracket/rate configuration
  (`testDs1Through12BracketEnergyConservation`,
  `testDs11DeterminismAcrossBracket`, `testDs12AllocationAcrossBracket`) are
  now also present.
- [x] DS-4: preserve bare-FDN NS-6 untouched. Fit and record full-path decay
  for a declared impulse/noise fixture at minimum Decay and high Damp, including
  fit window and method. Assert neither equality nor inequality with NS-6;
  report the measured result and leave interpretation to review.
- [x] DS-7: use deterministic broadband noise and Welch PSDs averaged over at
  least two complete segments. Record segment count/length, window, overlap,
  FFT length and silent-bin floor. Report MSC only at retained bins and report
  normalized cross-correlation at lag zero plus a declared symmetric short-lag
  range. Do not use impulse single-periodogram coherence or a zero-MSC gate.
- [x] DS-8/9: for each Mix fixture, record `E[L^2]`, `E[R^2]`, `E[L*R]`,
  `E[(L+R)^2]`, channel RMS, first nonzero arrival, full-path energy centroid,
  and isolated output-diffuser centroid. Label `+3.01 dB` as conditional on
  all-lag uncorrelatedness; do not set a channel-balance/perceptual tolerance
  before reviewing results. Powers/covariance (DS-8) and RMS/first
  arrival/full-path centroid (DS-9) are now recorded at every point of the
  same five-point Mix sweep, at both fixture rates. The isolated
  output-diffuser centroid is measured once per rate, not once per Mix, since
  its fixture never touches Mix (no FDN, no input chain, no dry/wet gain) and
  is Mix-invariant by construction; this is stated explicitly at the
  measurement site rather than left implicit.
- [ ] DS-10: demonstrate cutoff only at diffusion recursive-memory writes,
  wrapper fault aggregation, repeated full reset equivalence and
  silence-in/silence-out. For every section record stored float `q_j`, an
  explicit cessation-state bound `S_j`, and integer drain
  `D_j=(k_j+1)d_j` with `k_j=min{k>=1:q_j^k*S_j<1e-20F}`. Treat this as a
  proof template; separately measure actual full-path silence and do not
  compare it to the historical additive timeout. **Partial:** stored float
  `q_j`, input-chain analytic bounds, output measured-window illustrations,
  drains and an observed trailing exact-silence interval are recorded; no
  propagated whole-chain cessation-state proof exists. This is the one
  remaining open item on this line: a correct propagated bound needs the
  FDN's actual injection topology (the Hadamard matrix `A`, each line's
  folded gain `gᵢ/sqrt(N)` and its damping-filter state, per ADR-003) chained
  with the input cascade's own analytic peak-gain bound via a driven
  (not free) contraction argument — replacing the current output-section rows'
  `measuredTapPeak` with an analytically propagated one. That is a genuine
  numerical-safety derivation against ADR-002/003's exact state definitions,
  not a mechanical bracket extension, and was deliberately not attempted
  alongside the bracket-completion pass above: an incorrect "proof" here
  would be worse than the current honestly-labeled gap. It should be scoped
  as its own dedicated pass, reviewed at the same rigor as ADR-003 itself.
- [x] Run the focused target, all CTest suites, allocation instrumentation and
  `git diff --check`. Update `docs/testing.md` with commands, exit status,
  numeric results, fixtures, analysis settings and remaining gaps. The pass
  does not establish sonic acceptance.

## Review gates before any implementation authorization

1. Confirm this plan's wrapper, tap accessor and aggregate-detector contracts
   against the current FDN/automation source, including rollback and saturation
   behavior.
2. Approve a bounded task range; Task 1 alone is the maximum safe first code
   increment. Do not start Task 2 merely because Task 1 is green.
3. After Tasks 1–3, review measured DS-4/7/8/9/10 results before interpreting
   the baseline or selecting successor topology/taps.
4. Keep the existing mono renderer as a regression fixture. Any stereo renderer,
   host integration, UI or perceptual corpus needs its own authorization.

## Commands for a future authorized increment

```sh
cmake -S . -B build/ds-b -DCMAKE_BUILD_TYPE=Release
cmake --build build/ds-b --parallel
ctest --test-dir build/ds-b -R 'aetherfield_dsp_(fdn|diffusion_stereo)_tests' --output-on-failure
ctest --test-dir build/ds-b --output-on-failure
git diff --check
```

No command above has been run for DS-B because this document creates a plan,
not an implementation or evidence claim.
