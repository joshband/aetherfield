# Phase 1 S2 — Fixed late network: implementation task plan

**STATUS: Implemented and independently verified, 2026-09-16**, with two
design corrections discovered during implementation and applied in place
(the "Capacity" section and the coefficient-accessor additions below —
search this document for "Corrected during implementation" and "Added
during implementation"). This document was written, and remains preserved
below, as the task plan Terra's implementation was required to follow; for
current implementation and verification evidence see
[testing.md](testing.md)'s "IMPLEMENTED: Phase 1 S2" section. Every "not
created by this plan" / "future implementation" / "not authorized"
statement below describes this document's status before the owner
separately authorized S2 implementation.

This is the deliverable for roadmap.md Phase 1 row 4 ("Translate accepted
design into small implementation increments"), covering ADR-002's S2
milestone ("Fixed late network") for the **NS-1..NS-11 gate only**
(docs/phase1-s2-verification-plan.md, docs/decisions.md ADR-003 §(c)). It
does **not** cover the PT-1..PT-9 parameter-transition gate — that targets a
separate parameter-transport class, is a separate future plan, and is
authorized separately, exactly as phase1-s2-verification-plan.md's
build-target structure already keeps `aetherfield_dsp_fdn_tests` and
`aetherfield_dsp_param_tests` as two distinct executables for two distinct
failure modes.

## Non-goals (read first)

This document is a design/task plan only, per roadmap row 4's acceptance
bar: "implementation reserved for later authorization." It does **not**
authorize:

- Writing or editing any `.h`/`.cpp`/`CMakeLists.txt` file.
- A parameter-transport class, smoothing, automation, or anything from
  ADR-004's Mix/Decay/Damp control surface. This plan's `prepare()` takes
  raw `T60₀`/`T60_π` in seconds, not a normalized `[0,1]` Decay/Damp value —
  the mapping between them belongs to the separate, still-unplanned
  parameter-transport class.
- Input/output diffusion, stereo extraction, output tap design, or any
  product-facing signal path. ADR-002 explicitly defers all of these; the
  test-only injection/tap convention defined below (see "What this plan is
  *not* deciding") exists solely to make NS-1..NS-11 executable and must
  never be read as a product decision.
- Runtime-adjustable Size, pre-delay, interpolation, or **any modulation of
  any kind**. `mᵢ`, every `gᵢ`, every `aᵢ`, and the matrix `A` are constants
  of one `prepare()`, re-derived only on a full re-`prepare()`, exactly as
  ADR-003 (d) requires. ADR-002's prohibition and ADR-003 (e)'s Path A/Path B
  bars are restated, not reopened, by this plan.
- Choosing the product's `N` or its supported sample-rate matrix. This plan
  builds the class that ADR-005's `N_fixture ∈ {4, 8, 16}` bracket runs
  against; `N_product` and the product's rate matrix remain exactly as
  deferred as ADR-005 left them.

Implementation of the interfaces below is a future, separately authorized
step, exactly as phase1-s1-plan.md reserved `DelayLine.h`/`.cpp` for
separate authorization before S1 was authorized.

## What this plan additionally proposes: two small methods on `DelayLine`

`FeedbackDelayNetwork`'s per-sample recurrence needs each line's **current**
delayed output *before* it can compute what to write back into that line —
the matrix mixes all `N` lines' outputs together, so nothing can be written
anywhere until everything has been read. `DelayLine::process()` (already
implemented, tested, and committed in S1) does not support this: it takes
the value to write as an *input parameter* to the very call that reveals the
value being evicted, so composing `N` of them naively would require already
knowing each line's write-value before any line's read-value is available —
circular for a coupled network, even though it is exactly the right, and
only, primitive S1 needed for an uncoupled single line.

**Proposal: add two new methods to the existing `DelayLine`, and change
nothing about `process()`'s observable behavior or any of its 9 existing
tests.**

```cpp
// Returns the value currently held at the write position, without
// advancing or modifying any state. This is exactly the value process()
// would currently return as its "delayed" output if called right now with
// any input. Safe to call at any time, including before prepare() (returns
// 0.0F then, matching the reset state's zero fill and process()'s existing
// "or the initial zero" language).
// noexcept, allocation-free, branch count independent of sample values.
float peek() const noexcept;

// Writes value into the delay history at the current write position and
// advances the write position by one sample, wrapping modulo
// maxDelaySamples. Unlike process(), does not read or return the value
// being overwritten — call peek() first if that value is needed. Together,
// peek() then push() are process()'s existing single-sample behavior split
// into its two independent halves; process() itself is unchanged and may
// continue to be implemented however is clearest, including in terms of
// peek()/push() internally, as an implementation detail.
// Precondition: prepare() must have returned true at least once since
// construction — the same caller contract process() already documents, not
// a runtime-checked sanitizer.
// noexcept, allocation-free, branch count independent of sample values.
void push(float value) noexcept;
```

**Why this is safe to add now, not a reopening of S1.** `process()`'s
contract, and all 9 of `tests/DelayLineTests.cpp`'s existing cases, describe
only the *combination* of reading the old value and writing a new one in a
single call; nothing in S1's plan or evidence claims that combination is the
*only* operation `DelayLine` may ever expose. `peek()` is provably
side-effect-free (it reads `storage_[writePos_]` without touching it), and
`push()` is exactly `process()`'s existing write-and-advance half with the
read half removed. `process(input, output, count)` remains implementable,
unchanged, as `output[i] = peek(); push(input[i]);` repeated `count` times —
so adding these two methods cannot change any answer S1's evidence already
recorded.

## Exact interface: `FeedbackDelayNetwork`

File location for a future implementation (not created by this plan):
`src/dsp/FeedbackDelayNetwork.h` / `src/dsp/FeedbackDelayNetwork.cpp`,
`namespace aetherfield::dsp`, alongside `DelayLine.h`/`.cpp`.

```cpp
#pragma once

#include "dsp/DelayLine.h"

#include <array>
#include <cstddef>
#include <vector>

namespace aetherfield::dsp {

// A fixed (time-invariant, unmodulated) Feedback Delay Network: N delay
// lines, one fixed normalized-Hadamard orthogonal feedback matrix, one
// scalar per-line gain and one bounded-real one-pole damping filter per
// line, in series inside the loop. No modulation, no diffusion, no stereo
// strategy, no runtime-adjustable Size/pre-delay. See docs/decisions.md
// ADR-002 (topology), ADR-003 (damping/denormal/lifecycle), ADR-005 (N and
// the delay-length derivation rule this class implements internally).
class FeedbackDelayNetwork {
public:
    // The only line counts this class supports, per ADR-005: the Sylvester-
    // Hadamard orders in ADR-002's evaluation bracket. Any other N is
    // rejected by prepare(), not clamped to the nearest supported value.
    static constexpr std::size_t kMinLineCount = 4;
    static constexpr std::size_t kMaxLineCount = 16;

    // Constructs an unprepared object: no allocation, no lines.
    FeedbackDelayNetwork() noexcept;

    // The only allocation point, off the render thread, exactly as
    // DelayLine::prepare(). Preconditions, each independently checked and
    // each causing prepare() to return false with no allocation and no
    // change to any prior valid preparation, matching DelayLine's rule 7
    // (ADR-003 (d) rule 7):
    //
    //   sampleRate:    finite, > 0.0.
    //   lineCount:     one of {4, 8, 16} (kMinLineCount/kMaxLineCount name
    //                  the bracket's ends; 12 is not a supported value —
    //                  see ADR-005's Hadamard/Sylvester-order rationale).
    //   tMinSeconds,
    //   tMaxSeconds:   finite, > 0.0, tMinSeconds < tMaxSeconds. This is
    //                  ADR-002's "time-domain specification, not a sample
    //                  count" for the delay lengths; sample counts mᵢ are
    //                  derived here, never accepted as an argument, per
    //                  ADR-005 (b). The S2 test fixture passes ADR-005's own
    //                  decided values (0.027, 0.081); this class itself
    //                  stays general over any valid endpoints, since the
    //                  derivation *rule* is what ADR-002/005 fixed, not a
    //                  hard-coded constant belonging inside the DSP class.
    //   t60ZeroSeconds,
    //   t60PiSeconds:  finite, > 0.0, t60PiSeconds <= t60ZeroSeconds
    //                  (ADR-003 (a): "high frequencies may never decay more
    //                  slowly than DC" — this is what keeps every derived
    //                  aᵢ inside [0, 1) and therefore bounded-real).
    //                  t60PiSeconds == t60ZeroSeconds is damping bypass
    //                  (ADR-002 S2's "damping filters initially
    //                  bypassable"): it drives every aᵢ to exactly 0, per
    //                  ADR-003 (a), not a separate code path.
    //
    // On success: derives mᵢ for i in [0, lineCount) via ADR-005's rule
    // (nearest prime to a geometrically-spaced time target, tie-break to
    // the smaller prime, a strictly-increasing totality guard), asserts
    // every mᵢ is prime, strictly increasing and pairwise co-prime
    // (ADR-005; checked by direct gcd, never inferred from primality: see
    // "Validation" below), derives every gᵢ and aᵢ in double per ADR-003
    // (a)'s exact formulas, builds the normalized Hadamard matrix for
    // lineCount, prepares each internal line at exactly its own mᵢ (see
    // "Capacity" below for why this plan does not reserve extra headroom
    // beyond the active delay length), sets every damping filter's state to
    // 0, clears the non-finite counter's latched flag (not its count — see
    // reset()), and leaves the object in exactly the state reset() defines.
    // Not noexcept: allocation failure propagates, exactly as
    // DelayLine::prepare().
    //
    // A validation failure at any of the above returns false, performs no
    // allocation, and leaves the object exactly as it was before the call —
    // a prior valid preparation remains valid and usable (ADR-003 (d) rule
    // 7, restated from DelayLine's identical S1 rule).
    bool prepare(double sampleRate,
                 std::size_t lineCount,
                 double tMinSeconds,
                 double tMaxSeconds,
                 double t60ZeroSeconds,
                 double t60PiSeconds);

    // Zeros every line's delay history (each line's `DelayLine::reset()`),
    // zeros every damping
    // filter's recursive state to exactly 0 (ADR-003 (d) rule 3 — the state
    // S1 has no analogue for, and the one a missed reset leaves as an
    // audible decaying residue), restores every write position, and clears
    // the non-finite detector's latched flag. Per ADR-003 (c)'s mandatory
    // non-finite handling, restated here: the detector's *counter* is
    // deliberately NOT cleared by reset() — an intervention must remain
    // visible, never erasable by the same mechanism that would otherwise
    // hide it. Coefficients (gᵢ, aᵢ, A, mᵢ) are configuration, not state,
    // and are never touched by reset(). Idempotent, repeatable, safe before
    // prepare() (a no-op then), safe on the render thread: noexcept,
    // allocation-free, bounded work.
    void reset() noexcept;

    // Advances the network by count samples. For each sample: reads every
    // line's current delayed output via peek(); applies each line's
    // damping filter Hᵢ(z) to its own state; scales by gᵢ; applies the
    // fixed matrix A; adds this sample's input contribution (see "What this
    // plan is not deciding" for the test-only injection convention); pushes
    // the result into each line via push(); writes this sample's output tap
    // (see the same section). Every recursive memory (the value pushed into
    // each delay line, and each damping filter's retained state) passes
    // through the ADR-003 (b) deterministic cutoff, |x| >= 1e-20 ? x : 0,
    // before being stored — the only two places ADR-003 (b) names.
    //
    // On detecting a non-finite value about to be written into any delay
    // line or damping filter state: increments the non-finite counter
    // (saturating, never wrapping) and latches the flag, but does NOT clip,
    // substitute, or otherwise alter the value — ADR-003 (c) requires
    // detection to be a diagnostic, and testing.md requires any safety
    // intervention to be reported, never hidden by silently correcting it.
    // The network's future output is not meaningful after this; only a
    // subsequent reset() is a documented recovery path (ADR-003 (c)/(d)).
    //
    // count == 0 is always a safe no-op, matching DelayLine. input/output
    // are caller-owned; output may be null only when count == 0. input and
    // output may alias per line the same way DelayLine's do; this class
    // additionally requires input and output not overlap the network's own
    // per-sample scratch (an internal, class-owned, fixed-capacity array —
    // see "Scratch storage", never heap-allocated here).
    //
    // Precondition for count > 0: prepare() must have returned true at
    // least once, matching DelayLine's identical contract.
    //
    // noexcept, allocation-free, lock-free, branch count independent of
    // sample values, except for the two data-independent branchless cutoff
    // selects per line per sample that ADR-003 (b) itself specifies.
    void process(const float* input, float* output, std::size_t count) noexcept;

    // Diagnostic accessors, per ADR-003 (c)'s mandatory non-finite handling.
    // Both noexcept, allocation-free, safe on the render thread (a caller
    // may poll nonFiniteCount() from a UI/control thread; ADR-003 does not
    // require this counter itself to be transported lock-free beyond being
    // a single integral read, which is why it is exposed as a plain
    // std::size_t rather than through any parameter-transport mechanism —
    // that mechanism is out of scope for this plan regardless).
    std::size_t nonFiniteCount() const noexcept;
    bool nonFiniteLatched() const noexcept;

    // The orthogonality residual computed once at prepare() and retained as
    // a diagnostic (ADR-003 (d) rule 4: "its orthogonality residual is
    // computed once here and retained as a diagnostic"). This is exactly
    // NS-1's ||AᵀA - I||∞ figure, exposed so a test can read it without
    // recomputing the check against the class's own internal matrix
    // representation.
    float orthogonalityResidual() const noexcept;

private:
    std::size_t lineCount_ = 0;
    std::vector<DelayLine> lines_;             // size == lineCount_ once prepared
    std::vector<float> dampingState_;           // one float per line; the wᵢ[n-1] of ADR-003 (a)
    std::vector<float> dampingCoeffA_;          // aᵢ, one per line
    std::vector<float> dampingCoeffOneMinusA_;  // (1 - aᵢ), precomputed in double, stored as float
    std::vector<float> lineGain_;               // gᵢ, one per line
    std::vector<float> matrix_;                 // lineCount_ x lineCount_, row-major, the realized A
    std::array<float, kMaxLineCount> scratch_{}; // per-sample working vector; never resized on the render thread
    std::size_t nonFiniteCount_ = 0;
    bool nonFiniteLatched_ = false;
    float orthogonalityResidual_ = 0.0F;
};

} // namespace aetherfield::dsp
```

### Validation (ADR-003 §(c)'s table, applied to this class)

| Quantity | Rule | Failure behavior |
|---|---|---|
| `sampleRate` | Finite, `> 0.0` | `prepare` returns `false`; no change |
| `lineCount` | One of `{4, 8, 16}` | `prepare` returns `false`; no change |
| `tMinSeconds`, `tMaxSeconds` | Finite, `> 0.0`, `tMinSeconds < tMaxSeconds` | `prepare` returns `false`; no change |
| `t60ZeroSeconds`, `t60PiSeconds` | Finite, `> 0.0`, `t60PiSeconds <= t60ZeroSeconds` | `prepare` returns `false`; no change |
| Derived `mᵢ` | Prime, strictly increasing, pairwise co-prime (`gcd(mᵢ, mⱼ) = 1` for every `i ≠ j`, checked directly — `lineCount·(lineCount−1)/2 ≤ 120` gcds, off the render thread). Each `mᵢ` is passed directly as its own line's `DelayLine::prepare(sampleRate, mᵢ)` capacity (see "Capacity" below), so no separate `≤ capacity` check applies | A violation here would be a programming error in the derivation, not a caller input error; it still fails `prepare` per ADR-003 (c)'s "must never reach the render thread" rule rather than asserting/aborting |
| Derived `gᵢ`, `aᵢ`, `1 − aᵢ`, every entry of `A` | Computed in double, each checked finite and inside its stated interval before being stored as `float` | Same as above: fails `prepare`, never reaches the render thread |
| Input samples | Non-finite input sample is treated exactly as ADR-003 (c) specifies for the network generally: substituted with `0` for that sample's injection and counted via the same non-finite counter `process()` already exposes | No magnitude clamp, matching ADR-003's rationale for why none exists |

### Capacity: each line is prepared at exactly `mᵢ`, not at an inflated reserve

**Corrected during implementation (see docs/agent-log.md): an earlier draft
of this section proposed reserving `round(sampleRate * tMaxSeconds) + 64`
samples per line while using only `mᵢ` of them as the active delay length.
That is not expressible through `DelayLine`'s actual, already-shipped
interface: `DelayLine::prepare(sampleRate, maxDelaySamples)` makes
`maxDelaySamples` simultaneously the reserved capacity *and* the only active
delay length (see `src/dsp/DelayLine.h` — S1 has no separate capacity/active-
length concept, and this plan does not propose adding one). Preparing a line
with an inflated capacity would silently make it delay by that inflated
value instead of by `mᵢ`. The design below is the fix.**

**Each internal line is prepared at exactly its own `mᵢ`**: `lines_[i].
prepare(sampleRate, mᵢ)`. This is precisely ADR-005 (b)'s own second named
option — "prepares each line at exactly `mᵢ`" — which ADR-005 already
records as equally valid to reserving extra headroom, specifically *because*
"the purpose [of reserving above active `mᵢ`] is currently moot, ADR-004
defers both Size and Pre-delay." No extra headroom is reserved, for two
reasons stated together:

1. **It is not implementable without changing `DelayLine`'s shipped
   interface**, and this plan proposes no such change beyond `peek()`/
   `push()` (see above), which are orthogonal to capacity.
2. **It would buy nothing yet.** ADR-003 (d)'s rate-change rule already
   requires "a full re-`prepare`," "re-derivation of `mᵢ`... every `gᵢ` and
   every `aᵢ`," and "a full reset" that "discards, not resamples," on *any*
   rate change — there is no seamless, allocation-free rate transition this
   class attempts to support, so reserving headroom for a future rate or a
   future Size/pre-delay control buys nothing until one of those is actually
   implemented, at which point ADR-005's own "Revisit When" already names
   that as the event that reopens this rule.

**Consequence for `f_s_max`/highest-supported-rate reservation (ADR-003 (d)
rule 2):** with no headroom reserved at all, that rule is vacuously
satisfied per preparation (each line reserves exactly what it uses, for the
rate it was just prepared at) rather than satisfied by reserving in advance
for a higher future rate — consistent with there being no product
supported-rate matrix to reserve against (ADR-005 names this a real,
separate, still-open gap). Should Size, pre-delay, or a genuine multi-rate
reservation strategy later be accepted, this is the point that reopens.

## What this plan is *not* deciding: the NS-test injection/tap convention

NS-1..NS-11 require running actual signals — impulses, full-scale steps,
long stress runs — through the network (docs/decisions.md ADR-003 §(c)).
ADR-002 defers the product's input injection vector and output tap design
in full. Something must still exist for `process()` to have any effect at
all, so this plan fixes the smallest, most standard convention used to
exercise an FDN in isolation, and labels it exactly as what it is:

> **Uniform injection, summed tap — a test convention, not a product
> decision.** Each input sample is added identically to every line's
> pre-push value (`writeᵢ += input[n]`, no per-line weighting). The output
> tap is the unweighted sum of all `lineCount_` lines' `peek()` values for
> that sample, taken *before* any of them are modified this sample. Both
> choices are the standard way an FDN's own internal properties (decay law,
> stability margin, denormal behavior, determinism) are exercised in the
> DSP literature this project already cites (Smith; Jot and Chaigne)
> independent of any specific product's input/output diffusion design, and
> neither survives into any later stage: a real signal path's injection
> vector and tap design remain exactly as open as ADR-002 left them.

This convention is what NS-1 through NS-11 are run against; it is not
claimed to be, and must never be described as, Aetherfield's eventual wet
signal path.

## The matrix: normalized Hadamard via the Sylvester construction

ADR-002 selected a normalized Hadamard matrix as the baseline feedback
matrix; ADR-005 confirms `lineCount ∈ {4, 8, 16}` is exactly the set of
Sylvester-Hadamard orders in the evaluated range. The Sylvester construction
is the standard in-place fast Hadamard transform (a "butterfly" over
`log₂(lineCount)` stages, each stage `lineCount / 2` additions and
`lineCount / 2` subtractions — `lineCount · log₂(lineCount)` operations
total, ADR-002's stated cost):

```cpp
// Applies the un-normalized (+1/-1 entries) Hadamard transform to x in
// place. n must be a power of two. O(n log n) additions/subtractions, no
// multiplies, no allocation, branch count independent of the data.
void fastHadamardTransformInPlace(float* x, std::size_t n) noexcept {
    for (std::size_t len = 1; len < n; len <<= 1) {
        for (std::size_t i = 0; i < n; i += (len << 1)) {
            for (std::size_t j = i; j < i + len; ++j) {
                const float a = x[j];
                const float b = x[j + len];
                x[j] = a + b;
                x[j + len] = a - b;
            }
        }
    }
}
```

The **normalized** matrix `A` used in the stability proof requires
`AᵀA = I`, which the un-normalized `±1` Hadamard matrix `H` satisfies only
as `HᵀH = n·I`; the required `1/√n` scaling is **folded into each line's
gain `gᵢ`** at `prepare()` time (`gᵢ ← gᵢ / √lineCount_`) rather than
applied as a separate per-sample multiply, exactly as ADR-002 itself
suggests ("a single uniform scaling that can be absorbed into the per-line
attenuation gains"). `orthogonalityResidual()` is computed at `prepare()`
against the *normalized* matrix this folding implies, not against the raw
`±1` transform, since NS-1's bound is stated for `A`, not `H`.

## Preparation/lifecycle contract (summary)

Restates ADR-003 (d), specialized to this interface; nothing here changes
its meaning.

- **One allocation point.** `prepare()` is the only place this class
  allocates. Constructor, `reset()`, and `process()` never allocate.
- **Invalid preparation is rejected, not clamped**, per the Validation table
  above, and never disturbs a prior valid preparation.
- **A sample-rate change is a full re-`prepare()`.** There is no
  `setSampleRate()`, matching `DelayLine`. All retained delay contents and
  damping-filter state are discarded, not resampled — ADR-003 (d) rule 3,
  restated: this is a deliberate, documented consequence, not a seamless
  transition.
- **`reset()` guarantees**, restated as a checklist directly from ADR-003
  (d): every line's full reserved capacity zeroed; every write position
  restored; every damping filter's state set to exactly `0`; the non-finite
  flag cleared, its **counter not cleared**; coefficients and configuration
  untouched; idempotent; safe before `prepare()`; safe on the render thread.
  Because the zero state is an equilibrium (ADR-002), silence in yields
  *exactly* zero out — the canary for any of the above a future
  implementation gets wrong.

## Test plan

Future file: `tests/FdnTests.cpp`, `int main()`, no external test framework,
in the exact style of `tests/DelayLineTests.cpp`'s named-case/`fail()`
pattern. Each case below is exactly one row of
docs/phase1-s2-verification-plan.md's NS-1..NS-11 table, run against
`FeedbackDelayNetwork` prepared with `lineCount ∈ {4, 8, 16}` at
`sampleRate ∈ {48000, 44100}`, using ADR-005's decided `tMinSeconds = 0.027`,
`tMaxSeconds = 0.081`.

| # | Named case | NS ID | What it asserts |
|---|---|---|---|
| 1 | `testMatrixOrthogonality` | NS-1 | `orthogonalityResidual()` (and an independently recomputed `‖AᵀA − I‖∞` from the test's own reconstruction of the normalized matrix, not trusting the class's internal one) `≤ 1e-6` in `float`, `≤ 1e-12` recomputed in `double`, for every `lineCount` |
| 2 | `testBoundedRealnessExact` | NS-2 | For every line, `max(\|Hᵢ(1)\|, \|Hᵢ(−1)\|) ≤ 1` computed from the class's own stored `aᵢ`; `\|Hᵢ(1)\| == 1` and `\|Hᵢ(−1)\| == (1−aᵢ)/(1+aᵢ)` exactly, per ADR-003's two-point monotonicity lemma |
| 3 | `testBoundedRealnessFalsification` | NS-3 | Dense grid of `≥ 2¹⁶` points on `[0, π]`: `sup\|Hᵢ(e^{jω})\| ≤ 1 + 1e-6`, recomputed from `aᵢ` independently of case 2's exact check |
| 4 | `testMargin` | NS-4 | Recompute `ρ = σ_max(ΓA)` independently (e.g. via power iteration on `AᵀΓᵀΓA`); require `ρ ≤ maxᵢ gᵢ·(1 + 1e-6)` and `1 − ρ ≥ δ_margin` with `δ_margin = 1e-3` |
| 5 | `testCoefficientFiniteness` | NS-5 | Every stored `mᵢ`, `gᵢ`, `aᵢ` finite and inside its stated interval, at both fixture rates and at representative `T60₀` extremes for the delay set in use |
| 6 | `testDecayLaw` | NS-6 | Impulse response energy-decay curve vs. `10^(−3n/(f_s·T60₀))` at DC; realized `T60(ω)` vs. target in `≥ 8` bands; record the maximum band error in percent |
| 7 | `testWorstCaseCombination` | NS-7 | `T60₀ = T60_max` (computed per ADR-005's closed form for the delay set in use) × `t60PiSeconds == t60ZeroSeconds` (minimum damping, `aᵢ = 0`) × full-scale uniform-injection input, `≥ 120 s`, across block partitions `{1, 13, 64, 512, ragged}`, at both fixture rates: `nonFiniteCount() == 0` throughout; peak internal magnitude (scratch_ before each push) recorded; output overshoot reported, never clipped |
| 8 | `testFiniteTimeSilence` | NS-8 | Full-scale impulse, then zero input: every internal state (line contents and damping states) reaches **exactly** `0.0F` within the ADR-003 `T_silence` bound; measured time-to-silence recorded; time-to-first-denormal recorded with the cutoff temporarily disabled in a test-only build variant |
| 9 | `testDenormalCost` | NS-9 | Measured cost with the cutoff, without it, and (on the build host) with/without `FPCR.FZ` set around the measurement — four recorded numbers, no pass/fail threshold beyond "recorded" |
| 10 | `testDoublePrecisionReference` | NS-10 | Same fixture and injection reproduced in a `double`-typed reference path; compare against the `float` implementation within a stated, justified tolerance |
| 11 | `testDeterminism` | NS-11 | Two independent renders of the same fixture, same length, from a fresh `prepare()`/`reset()`, are byte-identical |

Test-only build variant needed for case 8's "cutoff disabled" measurement
(illustrative, not authorized source — written when S2 implementation is
authorized): a compile-time or link-time seam that bypasses exactly the two
`f_ε` selects ADR-003 (b) names, nothing else, so the comparison isolates
the cutoff's effect.

## Build wiring (future, not applied now)

Exactly docs/phase1-s2-verification-plan.md's already-specified structure;
this plan does not change it:

- Add `src/dsp/FeedbackDelayNetwork.cpp` to the existing `aetherfield_dsp`
  STATIC library sources.
- Add `src/dsp/DelayLine.cpp`'s `peek()`/`push()` additions to the same
  translation unit (no new file).
- `add_executable(aetherfield_dsp_fdn_tests tests/FdnTests.cpp)`, linked
  against `aetherfield_dsp`, same `-Wall -Wextra -Wpedantic -Werror` as
  every other target, registered as `add_test(NAME
  aetherfield_dsp_fdn_tests COMMAND aetherfield_dsp_fdn_tests)`.

None of the above is applied by this document.

## Ownership

Terra designs this specification: translating ADR-002/003/005's already-
decided math into a buildable interface with exact contracts, per roadmap
row 4. It decides no new architecture, topology, matrix family, damping
form, denormal mechanism, or numeric bound — every one of those is cited
back to its owning ADR, not re-derived. The two judgment calls this plan
does make — the `peek()`/`push()` addition to `DelayLine`, and the
uniform-injection/summed-tap test convention — are flagged explicitly as
such, with the reasoning that makes each checkable rather than asserted.
Luna independently validates this document against ADR-002/003/005 and
phase1-s2-verification-plan.md on disk, the same audit pattern already
applied to phase1-s1-plan.md, before any implementation is proposed for
authorization.

Actual test implementation — writing `tests/FdnTests.cpp`, the
`FeedbackDelayNetwork` and `DelayLine::peek()`/`push()` source, and the
corresponding `CMakeLists.txt` changes — is reserved for whenever S2
implementation is separately authorized, exactly as this document's
Non-goals section states.
