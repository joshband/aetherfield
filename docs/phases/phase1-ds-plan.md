# Phase 1 DS-A — fixed allpass primitive implementation plan

> **For agentic workers:** Use `superpowers:executing-plans` to implement
> this plan task by task. Steps use checkboxes for tracking. Independent
> verification remains required before milestone acceptance.

**Goal:** Build one reusable, fixed-delay Schroeder allpass section with
deterministic lifecycle, numerical diagnostics and measured response.

**Architecture:** Compose the existing `DelayLine` using `peek()` and `push()`.
Derive its fixed length from seconds during preparation. Return fault
information to the future owner instead of introducing another autonomous
reset policy or silently sanitizing an internal signal.

**Tech stack:** dependency-free C++20, existing CMake/CTest, host-only tests.

**Spec:** [ADR-006](../decisions/ADR-006-diffusion-stereo.md) (a), (b), (c), (h), subject to
[the implementation-readiness review](phase1-ds-review.md).

**Status:** IMPLEMENTED, 2026-09-17. This is the smallest independent
increment; it does not close the full DS gate. The implementation and focused
evidence are recorded in `docs/testing.md`; the full-chain follow-up below
remains future work.

## Global constraints

- `g_ap=(sqrt(5)-1)/2`; finite coefficient in `[0,0.9]`; fixed after prepare.
- `epsilon=1e-20F`, applied only to the value written to the delay memory.
- Time-domain prepare; nearest prime to `round(fs*seconds)`, lower on a tie.
- Allocation only during preparation; processing/reset are `noexcept` and
  allocation-free. The diagnostic result adds no logging or locks.
- No FDN, automation, Mix, modulation, wrapper, stereo renderer or UI changes.
- No claim of exact allpass behavior after the cutoff, constant exceptional
  branch counts, perceptual quality, or full-path decay/silence bounds.
- Preserve the inherited dirty ADR/supporting docs and `.codex/` configuration.
  At execution, use an isolated checkout carrying the approved specification;
  record its source revision and specification diff before starting.

## Files and interface

Create `src/dsp/SchroederAllpass.h`, `src/dsp/SchroederAllpass.cpp`, and
`tests/SchroederAllpassTests.cpp`. Modify `CMakeLists.txt` to add the library
source and `aetherfield_dsp_allpass_tests`, using the existing warning flags
and CTest registration pattern. Keep prime derivation local to this new
component for this increment; do not refactor the verified FDN as a side effect.

Proposed public API in `aetherfield::dsp`:

```cpp
class SchroederAllpass {
public:
    struct Sample { float value; bool nonFinite; };
    SchroederAllpass() noexcept = default;
    bool prepare(double sampleRate, double delaySeconds, double coefficient);
    Sample processSample(float input) noexcept;
    void reset() noexcept;
    std::size_t delaySamples() const noexcept;
    float coefficient() const noexcept;
};
```

Private state: one `DelayLine`, one stored float coefficient, one derived
sample count. No separate recursive filter state, count, latch or Mix gains.
`delaySamples()==0` before prepare; reset before prepare is harmless.
Processing requires successful preparation. `Sample::nonFinite` is true if
the input, peeked value, new memory value or output is non-finite. It is an
event indication, not a count of offending arithmetic operations. The future
wrapper decides aggregate counting and reset; this section neither substitutes
the input nor automatically resets its own tail.

Preparation rejects non-finite/nonpositive rate or time, non-finite coefficient,
coefficient outside `[0,0.9]`, and sample targets outside `[1,INT_MAX-1024]`.
The upper limit is a stated host-fixture implementation guard, not a supported
product-rate decision. Perform conversion only after checking the product.
Use `std::llround` and an overflow-safe primality loop `divisor <= n/divisor`.
Search upward/downward around the rounded center (minimum prime 2); lower
prime wins a tie. Reject if the nearest-prime search exceeds `INT_MAX`.
Allocate a candidate `DelayLine` before replacing live state; validation failure
returns false and allocation failure propagates without modifying the old
section. Commit the candidate by move only after success.

The test coefficient is intentionally an argument so DS-1 can exercise the
guard and `g=0` reference. It is not a runtime parameter or product control.

## Task 1 — recurrence and lifecycle

**Consumes:** `DelayLine::prepare(double,size_t)`, `peek()`, `push(float)`,
and `reset()`. **Produces:** the exact public API above and a CTest target.

- [x] Add the header usage and this first failing test to the new executable;
  register it in CMake. Build and confirm it fails because the new class has
  no implementation, rather than because of an unrelated environment issue.

```cpp
SchroederAllpass section;
const double golden = (std::sqrt(5.0) - 1.0) / 2.0;
if (!section.prepare(48000.0, 0.001, golden)) return fail("prepare");
if (section.delaySamples() != 47) return fail("nearest prime");
const double g = section.coefficient();
for (std::size_t n = 0; n <= 4 * 47; ++n) {
    const auto sample = section.processSample(n == 0 ? 1.0F : 0.0F);
    double expected = 0.0;
    if (n == 0) expected = -g;
    else if (n % 47 == 0)
        expected = (1.0-g*g)*std::pow(g, double(n/47-1));
    if (sample.nonFinite || std::abs(sample.value-expected) > 2e-6)
        return fail("analytic impulse mismatch");
}
```

- [x] Implement preparation and the recurrence without changing `DelayLine`.
  The numerical ordering is explicit:

```cpp
const float delayed = delay_.peek();
const float state = input + coefficient_ * delayed;
const float output = delayed - coefficient_ * state;
const bool bad = !std::isfinite(input) || !std::isfinite(delayed)
              || !std::isfinite(state) || !std::isfinite(output);
const float stored = !std::isfinite(state) ? state
                   : (std::fabs(state) >= 1e-20F ? state : 0.0F);
delay_.push(stored);
return {output, bad};
```

- [x] Extend the test with `g=0` (exact pure delay), `g=0.9`, both fixture
  rates, silence after reset, repeated reset, and failed-prepare preservation.
  For preservation, run identical prefix inputs on two sections, reject bad
  preparation on only one, then require bit-identical suffix outputs.
  Bad preparations cover NaN/Inf/zero/negative rate and time, overflowing
  products, and NaN/Inf/negative/>0.9 coefficient.
- [x] Test prime ties independently: 48 samples chooses 47; 176 chooses 173;
  205 chooses 199. Use seconds `target/fs`, and assert the realized count.
  Re-prepare at 44.1 kHz and 1 ms must yield 43 and discard all prior tail.
- [x] Build/run the new CTest target, then run all existing tests; commit
  only this task's files after checking the diff.

## Task 2 — independent response and numerical evidence

**Consumes:** the public primitive API from Task 1. **Produces:** evidence in
`SchroederAllpassTests.cpp` and `docs/testing.md`, with limits stated explicitly.

- [x] Add an independent double recurrence in the test executable. Drive it
  and the production section with an impulse and deterministic bounded noise.
  Compare outputs with absolute tolerance `2e-5`, reporting the maximum error.
  Derive reference coefficients from the stored float value so coefficient
  rounding is not mistaken for a recurrence defect.
- [x] Measure the impulse response, not only the transfer formula. Drain
  each section until at least `128*delaySamples()` samples, zero-pad to a
  power of two with at least 131072 samples, and transform with a test-local
  radix-2 double FFT. Validate that FFT first with an impulse (all bins one)
  and a delayed impulse (analytic phase). Check at least 65536 uniformly
  spaced frequencies over `[0,pi]` against unit magnitude within `1e-6`.
  Exercise every default input/output delay in ADR-006 at both rates.
  If the tolerance fails, diagnose truncation/rounding/implementation; never
  relax it just to obtain green results.
- [x] Accumulate energies in double for impulse and 4096 samples of fixed-seed
  noise followed by `128*d` zeros. Require relative input/output energy error
  `<=1e-5` for nonzero input energy. This tolerance covers float arithmetic
  over this finite fixture; it is not a sonic threshold.
- [x] Compute the exact single-section impulse in double, reverse its signs
  to construct a bounded adversarial input `x[n]=sign(h[M-n])`, `M=64*d`.
  Record the peak and require `peak <= 1+2*abs(g)+1e-5`. This is a section
  bound; do not label it the cascade's measured 25x/5x bound.
- [x] For an impulse and `0<g<=0.9`, require exact silence after
  `(ceil(log(1e-20)/log(double(g)))+3)*d` samples and for two further delay
  cycles. The extra cycles cover the initial write and a conservative float
  rounding margin for this unit-input fixture. Treat this as a fixture test,
  not the missing arbitrary-state cascade proof. Handle `g=0` separately:
  the delayed impulse ends after `d+1` samples.
- [x] Feed finite `FLT_MAX` for more than two delay cycles at `g=0.9` to
  exercise overflow; require a fault indication, never a silent correction.
  Also feed NaN/Inf and require fault indication. Reset, then require exact
  silence and no new fault. No logging/allocation is allowed inside processing.
- [x] Instrument allocations using test-local `new/new[]` including aligned
  variants, preallocate all input/results, and require zero count delta
  through processing and reset. Render identical scripts twice and under
  partitions `{1,13,64,512,ragged}` using repeated `processSample()` calls;
  compare output float bits. Record runtime as observational, not a budget.
- [x] Run the full suite, review the actual recurrence/cutoff placement,
  record results, and commit the focused task. Verification and documentation
  are complete; this session intentionally leaves the shared working tree
  uncommitted so the existing documentation changes remain together. A pass closes
  only section portions of DS-1/2/4/5/10/11/12, not the full-chain cases.

## Commands and final acceptance

```sh
cmake -S . -B build/ds-a -DCMAKE_BUILD_TYPE=Release
cmake --build build/ds-a --parallel
ctest --test-dir build/ds-a -R aetherfield_dsp_allpass_tests --output-on-failure
ctest --test-dir build/ds-a --output-on-failure
git diff --check
```

After implementation, expect five CTest executables passing, actual response
and fault evidence recorded, and no changed legacy DSP behavior. Update
`docs/dsp-design.md`, `docs/testing.md`, `docs/roadmap.md`, and the local
`docs/site/index.html` status together. Correct `docs/architecture.md`'s stale
S1-only description against source; append `docs/agent-log.md` only when a
milestone commit exists. Do not publish the website as part of this increment.

## Full-chain follow-up: coverage retained, not silently waived

The integration plan follows correction of R1–R5 in the review. Its required
coverage is recorded here so the primitive cannot be mistaken for completion.

| Cases | Required later deliverable |
|---|---|
| DS-1/2 | All configured cascades, actual stored coefficients, measured responses |
| DS-3 | Both window derivations, increasing guard, direct gcd across diffusion and network sets, all brackets/rates |
| DS-4 | Cascade energy, unchanged network NS-6, separately characterized full-path decay |
| DS-5 | Actual four-stage/two-stage peak bounds and adversarial renders |
| DS-6 | Amplitude-aware measured density and separately labeled lattice counts |
| DS-7 | Coherence estimator plus broadband/short-lag correlation, three decays and both rates |
| DS-8 | Sum spectrum/power, covariance, full Mix sweep and shared-gain listening files |
| DS-9 | Measured RMS, first arrivals and centroid, without an unsupported equality gate |
| DS-10 | Aggregate detector, whole-path reset and validated cascade silence bound |
| DS-11 | Fixed-parameter bit identity across blocks; automation separately follows PT-5 |
| DS-12 | Full-path allocation/cost evidence and every stage-count bracket runnable |

Integration should compose a mono-input/stereo-output owner around the existing
FDN and `ParameterAutomation`; keep the current mono renderer as a regression
fixture. A const tap accessor must read pre-step line values. At each sample,
advance automation exactly once, read taps before FDN advancement, inject the
diffused and `1/sqrt(N)`-scaled input, diffuse the taps, then apply the returned
Mix gains. Define wrapper preparation rollback and fault accounting before
fixing that owner's public interface. These are future plan obligations,
not undefined methods for an implementer to guess during DS-A.
