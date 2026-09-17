# Phase 1 parameter transitions — implementation task plan

**STATUS: Implemented and independently verified, 2026-09-17**, with one
design correction (an allocation-avoidance improvement to `publish()`, not
a contract change) and two test-design bugs caught and fixed during
implementation — see docs/testing.md's "IMPLEMENTED: Phase 1 parameter
transitions" section for full detail. This document was written, and
remains preserved below, as the task plan Terra's implementation was
required to follow; every "not created by this plan" / "future
implementation" / "not authorized" statement below describes this
document's status before the owner separately authorized implementation.

This is the deliverable for roadmap.md Phase 1 row 4 ("Translate accepted
design into small implementation increments"), covering ADR-004's
automation/transport/smoothing design (§(b)–(d)) for the **PT-1..PT-9 gate
only** (docs/phases/phase1-s2-verification-plan.md, docs/decisions.md ADR-004
§(d)). It does not revisit S1 or S2, which are separately implemented and
verified (testing.md).

## Non-goals (read first)

This document is a design/task plan only. It does **not** authorize:

- Writing or editing any `.h`/`.cpp`/`CMakeLists.txt` file.
- Any new architecture, mapping, smoothing law, or transport mechanism.
  Every formula below is transcribed from ADR-004, not re-derived with
  different numbers.
- Modulation of any kind. ADR-004 (d) restates ADR-002/ADR-003 (e)'s
  prohibition in full and this plan restates it again below; nothing here
  moves modulation closer to authorization.
- Deciding `D_max` as a product/perceptual value. ADR-004 assigns that to
  testing.md's Sonic acceptance gate. This plan picks an explicit,
  labeled **test-fixture** value only (see "The `D_max` fixture value"
  below), on grounds that carry no stability stakes — ADR-004 already
  proves every `D ≥ 0` is stable, bounded-real, and moves the network in
  the safe direction, so this is not the kind of consequential numeric
  choice ADR-005 was for `N`.
- Size, pre-delay, Mod Depth, Mod Rate, or any other deferred charter §14
  candidate. ADR-004 (a) defers all of them; this plan does not reopen
  that.

Implementation of the interfaces below is a future, separately authorized
step, exactly as phase1-s1-plan.md and phase1-s2-plan.md were before their
own implementations were authorized.

## Closing two of ADR-004's three deferred numbers, for the ADR-005 fixture

ADR-004 (b) fixed `T60_min`/`T60_max` as **derivations**, not values,
pending the delay set ADR-005 has since decided. Both are now pure
arithmetic on ADR-004's own already-decided formulas — no new judgment,
just evaluating them:

> `T60_max` = the largest `T60₀` satisfying `1 − γ₀^{m_min} ≥ δ_margin`,
> `δ_margin = 1e-3` (ADR-003). Closed form: `T60_max = 3·m_min / (f_s ·
> (−log₁₀(1 − δ_margin)))`.
>
> `T60_min` = the smallest `T60₀` for which `γ₀^{m_max}` remains a normal
> `float` (ADR-004 (b)). Closed form, by the same substitution:
> `T60_min = 3·m_max / (f_s · (−log₁₀(FLT_MIN)))`, `FLT_MIN ≈
> 1.1754944e-38`.

For the ADR-005 fixture (`N = 8`, `t_min = 27ms`, `t_max = 81ms`):

| Rate | `m_min` | `m_max` | `T60_min` | `T60_max` |
|---|---|---|---|---|
| 48000 Hz | 1297 | 3889 | **6.408 ms** | **186.560 s** |
| 44100 Hz | 1193 | 3571 | **6.405 ms** | **186.776 s** |

(`T60_max` at 48 kHz matches ADR-005's own quoted "≈186.6 s" to the
precision it was stated at — this is the same number, evaluated exactly,
not a new figure.) These four values are reproducible from the closed
forms above and the ADR-005 delay set alone; nothing new is decided by
computing them.

**`T60_min`'s stated purpose is qualitative** ("the network is still a
decaying tail rather than a diffuser," ADR-004 (b)) **and this plan does
not strengthen that into a numeric claim.** `T60_min` is decided exactly as
ADR-004 defined it — the point below which `γ₀^{m_max}` stops being a
normal `float` — and nothing more; whether 6.4 ms actually sounds like "a
decaying tail" is a Sonic acceptance question, not one this arithmetic
answers.

## The `D_max` fixture value

`D_max` is genuinely, and rightly, deferred as a *perceptual* choice
(ADR-004 (b): "a purely perceptual choice and belongs to testing.md's
Sonic acceptance gate"). But ADR-004 also proves, without qualification,
that **every `D ≥ 0` is stable** — Damp's mapping is safe at any value, so
picking a number to exercise PT-1/PT-2/PT-7/PT-9 carries none of the
stability stakes ADR-005's `N` choice did. This plan picks
**`D_max_fixture = 48 dB`** for test purposes only — large enough to
exercise real damping range, far below where ADR-003's `a_max = 0.999`
clamp binds ("hundreds of dB," ADR-003 Rationale) so the clamp's *absence*
of effect at this fixture value is itself part of what PT-2 checks — and
explicitly **not** a claim about what Aetherfield's Damp control should
feel like. `D_max_product` remains exactly as deferred as ADR-004 left it.

## What this plan additionally proposes: three small additions to `FeedbackDelayNetwork`

`FeedbackDelayNetwork` (S2, already implemented) derives `gᵢ`/`aᵢ` once at
`prepare()` and holds them fixed for the object's lifetime, per ADR-002/
ADR-003's time-invariant design — correct for S2, and exactly why S2 could
be verified as a static network. Automation requires those same
coefficients to become **externally driven and time-varying**, advanced by
this plan's smoother once per sample, while every stability property S2
already proved continues to hold at *each* frozen instant (ADR-004 (d)
point 6: "every frozen configuration along a ramp is Schur-stable with a
uniform margin"). Three small, non-breaking additions make this possible
without touching S2's own matrix/damping/cutoff logic or invalidating any
of its 11 existing tests:

```cpp
// The per-sample body of process(), extracted so an external automation
// layer can update coefficients between samples. process(input, output,
// count) remains implementable, unchanged, as
// output[i] = processSample(input[i]) repeated count times — exactly the
// same non-breaking-extraction argument S1's peek()/push() addition used.
float processSample(float input) noexcept;

// Overwrites line i's folded gain (gᵢ / sqrt(lineCount())) outside
// prepare(). Render-thread-safe: no allocation, no lock, noexcept. The
// caller (this plan's smoother) is responsible for supplying an
// already-validated, already-smoothed value — a caller contract, not a
// runtime-checked sanitizer, matching this codebase's existing style.
// Precondition: line < lineCount().
void setLineGain(std::size_t line, float foldedGain) noexcept;

// Overwrites line i's damping coefficient as a SINGLE value
// cᵢ = 1 − aᵢ, deriving aᵢ = 1 − cᵢ internally in the same call. This is
// deliberately not two independent setters for aᵢ and 1−aᵢ: ADR-004 (c)
// specifically measured that ramping both independently drifts them
// apart (up to 7.5e-5 in |Hᵢ(1)|, two orders of magnitude outside NS-3's
// tolerance) and requires cᵢ to be the one ramped quantity, with the
// other always derived from it in the same operation. Render-thread-safe,
// noexcept. Precondition: line < lineCount(), cᵢ in [1 − a_max, 1] =
// [0.001, 1] (the caller's responsibility, per ADR-004 (c) point 6 — the
// convex interval a linear ramp between two valid endpoints cannot leave).
void setDampingCoefficientC(std::size_t line, float c) noexcept;
```

**Why this is safe to add, not a reopening of S2.** All three methods are
additive; `process(count)`'s existing contract, and all 11 of
`tests/FdnTests.cpp`'s NS-1..NS-11 cases, describe only the already-shipped
behavior when these new setters are never called (S2's own tests never
call them). `processSample()` is provably equivalent to `process()`'s
existing per-sample body — this is the same non-breaking-extraction
argument already used and independently verified for `DelayLine::peek()`/
`push()`.

## Exact interface: `ParameterAutomation`

File location for a future implementation (not created by this plan):
`src/dsp/ParameterAutomation.h` / `src/dsp/ParameterAutomation.cpp`,
`namespace aetherfield::dsp`, alongside `FeedbackDelayNetwork.h`/`.cpp`.

```cpp
#pragma once

#include "dsp/FeedbackDelayNetwork.h"

#include <atomic>
#include <cstddef>
#include <vector>

namespace aetherfield::dsp {

// Validation, coefficient derivation (ADR-004 (b)), lock-free single-
// writer/single-reader transport with a generation counter (ADR-004 (d)),
// and render-thread linear-ramp smoothing (ADR-004 (c)) for Aetherfield's
// three accepted controls: Mix, Decay, Damp. Automates an already-prepared
// FeedbackDelayNetwork via its setLineGain()/setDampingCoefficientC().
// See docs/decisions.md ADR-004, docs/phases/phase1-pt-plan.md.
class ParameterAutomation {
public:
    ParameterAutomation() noexcept;

    // Control-thread-and-preparation-time call, off the render thread.
    // sampleRate/lineCount/tMinSeconds/tMaxSeconds MUST exactly match the
    // FeedbackDelayNetwork this object will automate, since T60_min/
    // T60_max derive from that exact delay set (ADR-004 (b)). dMaxDb is
    // this plan's test-fixture value (48.0) or another explicitly labeled
    // non-product value; never a value presented as a product decision.
    //
    // On success: computes T60_min/T60_max (this document's closed
    // forms), sizes the transport's atomic arrays and the render-thread
    // ramps to 2*lineCount + 2 elements, sets every control to its
    // default (Decay = 0.5, Damp = 0, Mix = 1.0 — full wet, matching S2's
    // own fixture convention of no dry path), publishes an initial
    // generation, and leaves the object in exactly the state reset()
    // defines. Not noexcept: allocation failure propagates.
    //
    // Preconditions, each independently checked, each causing prepare()
    // to return false with no allocation and no change to any prior valid
    // preparation: sampleRate finite > 0.0; lineCount one of {4, 8, 16};
    // tMinSeconds, tMaxSeconds finite > 0.0, tMinSeconds < tMaxSeconds;
    // dMaxDb finite and >= 0.0 (ADR-004: every D >= 0 is stable; a
    // negative D is not a "less damping" request the mapping can express,
    // since D(h) = D_max * h with h in [0,1] cannot go negative).
    bool prepare(double sampleRate, std::size_t lineCount,
                 double tMinSeconds, double tMaxSeconds, double dMaxDb);

    // ---- Control-thread API (ADR-004 (b), (d)) ----
    // Sets a new normalized [0,1] target. A non-finite value is REJECTED:
    // the published coefficient set and generation are left unchanged,
    // and false is returned. A finite out-of-range value is CLAMPED to
    // [0,1] and accepted. On acceptance: derives the full coefficient set
    // in double from the (possibly just-clamped) value and the other two
    // controls' last-set values, per ADR-004 (b)'s exact formulas, stores
    // each derived target into the atomic array, and release-publishes an
    // incremented generation. Not real-time safe (uses double math and
    // is meant to run on a control/UI thread); never called concurrently
    // with itself (single-writer, per ADR-004 (d)).
    bool setDecay(double normalized) noexcept;
    bool setDamp(double normalized) noexcept;
    bool setMix(double normalized) noexcept;

    // ---- Render-thread API (ADR-004 (c), (d)) ----
    // Once per block (or once per sample; both are correct): acquire-
    // loads the generation. If it differs from the one last consumed,
    // copies each atomic target into the corresponding ramp as a NEW
    // target — starting a fresh L-sample ramp from each ramp's CURRENT
    // value (ADR-004 (c) point 3), never resetting a ramp already in
    // flight toward a still-current target. This is the "2N + 2 more"
    // work ADR-004 (d)'s transport section describes; on an unchanged
    // generation this call only performs the one atomic load.
    void checkForNewTargets() noexcept;

    // Advances every one of the 2*lineCount()+2 smoothers by exactly one
    // sample (one add per coefficient, unconditional, per ADR-004 (c)
    // point 1 — zero when settled, no branch on ramp state), applies the
    // resulting per-line gain/damping coefficient to `network` via its
    // setLineGain()/setDampingCoefficientC(), and returns this sample's
    // dry/wet mix gains for the CALLER to apply outside network's own
    // process — Mix is structurally outside the feedback loop (ADR-004
    // (d) point 1) and FeedbackDelayNetwork itself has, and needs, no
    // concept of dry/wet.
    //
    // noexcept, allocation-free, lock-free (checkForNewTargets()'s atomic
    // load plus this call's fixed 2N+2 adds), branch count independent of
    // sample values except for the atomic generation-changed check, which
    // is itself data-independent of any *sample* value (ADR-004 (d)).
    struct MixGains {
        float dry;
        float wet;
    };
    MixGains advance(FeedbackDelayNetwork& network) noexcept;

    // Snaps every smoother to its current target and cancels any in-
    // flight ramp (ADR-004 (c) point 5). Does not touch `network` itself
    // — the caller is expected to reset() the network separately, exactly
    // as ADR-004 (c) point 5's silence-in/silence-out argument requires
    // both to happen together. Idempotent, safe before prepare() (a
    // no-op then), noexcept, allocation-free.
    void reset() noexcept;

    // Diagnostics.
    std::size_t nonFiniteRejectionCount() const noexcept; // control-thread rejections (ADR-003 (c)/ADR-004 (b))
    double t60Min() const noexcept;
    double t60Max() const noexcept;
    double dMax() const noexcept; // this object's fixture/product value, whichever prepare() received

private:
    // One linear ramp: reaches target exactly by assignment after
    // rampLengthSamples steps from whatever value it held when retargeted
    // (ADR-004 (c) points 2-3). No separate "in flight" flag: remaining
    // == 0 and current == target are the same state.
    struct Ramp {
        float current = 0.0F;
        float target = 0.0F;
        float increment = 0.0F;
        std::size_t remaining = 0;
    };

    void startRamp(Ramp& ramp, float newTarget) noexcept; // per ADR-004 (c) point 3
    void stepRamp(Ramp& ramp) noexcept;                    // per ADR-004 (c) points 1-2

    std::size_t lineCount_ = 0;
    std::size_t rampLengthSamples_ = 0; // L = max(1, round(tau_ramp * sampleRate)), tau_ramp = 20ms (ADR-004 (c))
    double t60Min_ = 0.0;
    double t60Max_ = 0.0;
    double dMax_ = 0.0;
    double lastDecay_ = 0.5;
    double lastDamp_ = 0.0;
    double lastMix_ = 1.0;

    // Transport: N gains + N damping coefficients + dry + wet.
    std::vector<std::atomic<float>> targetGains_;
    std::vector<std::atomic<float>> targetDampingC_;
    std::atomic<float> targetDry_{1.0F};
    std::atomic<float> targetWet_{0.0F};
    std::atomic<std::uint64_t> generation_{0};
    std::uint64_t consumedGeneration_ = 0;

    // Render-thread smoothers, mirroring the transport 1:1.
    std::vector<Ramp> gainRamps_;
    std::vector<Ramp> dampingRamps_;
    Ramp dryRamp_;
    Ramp wetRamp_;

    std::size_t nonFiniteRejectionCount_ = 0;
};

} // namespace aetherfield::dsp
```

### Coefficient derivation (control thread, in double — ADR-004 (b))

Transcribed exactly; no number here differs from ADR-004:

```
gamma0 = 10^(-3 / (f_s * T60_0(d)))
gammaPi = 10^(-3 / (f_s * T60_pi(d, h)))
T60_0(d) = T60_min * (T60_max / T60_min)^d          # d assigned exactly at d=0,1
D(h) = D_max * h                                     # h assigned exactly at h=0,1
T60_pi(d, h) = T60_0(d) / (1 + D(h) / 60)
for each line i:
    g_i_raw = gamma0 ^ m_i
    beta_i = (gammaPi / gamma0) ^ m_i
    a_i = clamp((1 - beta_i) / (1 + beta_i), 0.0, a_max = 0.999)
    c_i = 1 - a_i                                    # the ramped quantity
    g_i_folded = g_i_raw / sqrt(lineCount)            # matrix normalization, as in S2
dry(m) = cos(pi * m / 2)                              # m assigned exactly at m=0,1
wet(m) = sin(pi * m / 2)
```

Every `m_i` used above is read from the already-prepared `FeedbackDelayNetwork`
via its `delaySamples(i)` accessor (added during S2's implementation) — this
plan introduces no second source of the delay set.

### Validation (ADR-003 (c) / ADR-004 (b), applied to this class)

| Quantity | Rule | Failure behavior |
|---|---|---|
| `sampleRate`, `lineCount`, `tMinSeconds`, `tMaxSeconds` | Same as `FeedbackDelayNetwork::prepare()` | `prepare()` returns `false`; no change |
| `dMaxDb` | Finite, `>= 0.0` | `prepare()` returns `false`; no change |
| `setDecay`/`setDamp`/`setMix` argument | Non-finite **rejected**, not clamped; finite out-of-range **clamped** to `[0,1]` | Rejection leaves the published set/generation unchanged and increments `nonFiniteRejectionCount()`; it never reaches the render thread |
| Derived `g_i_raw`, `a_i`, `c_i`, `g_i_folded`, `dry`, `wet` | Computed in double, each checked finite before being stored as the published `float` target | A non-finite derived coefficient is a programming error (e.g. a bad `T60_min`/`T60_max`); the corresponding `set*` call returns `false` rather than publishing it |

## Test plan

Future file: `tests/ParameterTransitionTests.cpp`, `int main()`, no
external test framework, in the exact style of `tests/FdnTests.cpp`. Each
case is exactly one row of docs/phases/phase1-s2-verification-plan.md's PT-1..
PT-9 table, run against a `FeedbackDelayNetwork` (`N = 8`, 48 kHz, ADR-005's
delay set) automated by a `ParameterAutomation` prepared with
`dMaxDb = 48.0` (this document's fixture value).

| # | Named case | PT ID | What it asserts |
|---|---|---|---|
| 1 | `testEndpointExactness` | PT-1 | `setDecay(0)`/`setDecay(1)`, `setDamp(0)`/`setDamp(1)`, `setMix(0)`/`setMix(1)`; after `rampLengthSamples_` samples of `advance()`, the realized coefficients equal the endpoint constants bit-exactly. At `h=0`: `c_i == 1.0F` exactly and network output matches a damping-bypassed `FeedbackDelayNetwork` reference bit-for-bit. At `m=0`: wet contribution exactly 0; at `m=1`: dry contribution exactly 0 |
| 2 | `testInvariantUnderEveryReachableSet` | PT-2 | Dense sweep of `(d, h)`, including deliberately mismatched generations (call `setDecay` and `setDamp` without an intervening `advance()`/`checkForNewTargets()` to force a torn read): every derived `beta_i in (0,1]`, `a_i in [0, a_max]`, `c_i in [1-a_max, 1]`, `g_i <= g_max`, all finite, and `T60_pi <= T60_0` with no clamp applied (structurally true by `D(h) >= 0`) |
| 3 | `testInvalidValues` | PT-3 | `setDecay(NAN)` etc. return `false`, leave the generation unchanged (checked via a second `advance()` producing identical ramps), and increment `nonFiniteRejectionCount()`; `setDecay(1.5)` returns `true` and clamps to `1.0` |
| 4 | `testRepeatedRetargetingAtMaximumRate` | PT-4 | Two sub-cases, matching ADR-004 (d)'s "new target every block *and* every sample" verbatim, since they exercise different behavior: **(a) every-block** — call `setDamp`/`setDecay` once per block (a normal host-automation cadence) and `checkForNewTargets()` once per block, alternating range extremes; **(b) every-sample** — call `set*` every sample, faster than any single `checkForNewTargets()` cadence could consume one-for-one, specifically to exercise generation coalescing (later same-block targets overwrite earlier ones; no target is ever lost *permanently*, only superseded). Both sub-cases run at `T60_0 = T60_max`, full-scale input, for >= `10 * T60_max` samples, at both fixture rates: `nonFiniteCount() == 0` on the network throughout; peak internal magnitude recorded; output growth recorded relative to an unautomated reference held at the worst endpoint |
| 5 | `testChangingBlockPartitions` | PT-5 | Same automation script replayed under block partitions `{1, 13, 64, 512, ragged}`, concretized as `{1, 13, 64, 512, 977}` — `977` is prime, so it aligns with neither a power of two nor `rampLengthSamples_`, matching the same "ragged" concretization `tests/FdnTests.cpp`'s NS-7 already uses (calling `advance()` once per sample regardless of the caller's block size, since automation only depends on sample count, not partition); record the bounded difference against a stated tolerance rather than requiring byte-identical output; within one partition, repeated renders are byte-identical |
| 6 | `testResetHasNoDiscontinuity` | PT-6 | Start a ramp mid-flight, call both `automation.reset()` and `network.reset()`; assert no ramp is in flight (`advance()` immediately after produces the already-current target, unchanged); silence-in yields exactly zero out; `reset(); reset()` indistinguishable from one; a subsequent impulse response matches the *target* configuration exactly, not a mixture |
| 7 | `testSignalTransitionMetrics` | PT-7 | Endpoint-to-endpoint sweep of each control over `rampLengthSamples_` samples, on a full-scale sine and on impulse-plus-silence; record maximum sample-to-sample output difference and compare against a much slower reference sweep. No audition is performed by this automated test — recorded for a human review, per the gate |
| 8 | `testNoAllocationOrBlocking` | PT-8 | `static_assert(std::atomic<float>::is_always_lock_free)`; global `operator new`/`delete` instrumentation (the same pattern `DelayLineTests.cpp` case 9 uses) shows zero allocations across any number of `set*`/`advance()`/`checkForNewTargets()` calls. The verification plan's literal bound is per-sample **instruction and branch count** identical between a no-change and a change-every-sample run — a portable C++ test cannot measure retired-instruction or branch counts without platform-specific profiling counters (`perf`, Instruments), which this plan does not add, matching NS-9's identical, already-recorded limitation. What the source *does* guarantee, and what this test asserts directly by inspection rather than by measurement: `advance()`'s per-sample work is `2*lineCount()+2` unconditional adds with no branch on ramp state (ADR-004 (c) point 1 — "the increment is zero when settled, so there is no branch on whether a ramp is in flight"), so instruction/branch-count identity is a structural property of the source, not something this test independently measures. Wall-clock cost between the two runs is additionally recorded, not gated, as corroborating (not substitute) evidence |
| 9 | `testDampInvariantUnderDecayAndRate` | PT-9 | At both fixture rates and >= 3 values of `T60_0`, the measured excess attenuation at Nyquist over the `T60_0` window matches `D(h)` within a stated tolerance, confirming the mapping's delay-length/rate/Decay independence (ADR-004 (b)'s "homogeneous quantity in Jot's sense" claim) |

## Build wiring (future, not applied now)

- Add `src/dsp/ParameterAutomation.cpp` and `FeedbackDelayNetwork`'s three
  new methods (in its existing `.cpp`) to the `aetherfield_dsp` STATIC
  library sources.
- `add_executable(aetherfield_dsp_param_tests tests/ParameterTransitionTests.cpp)`,
  linked against `aetherfield_dsp`, same `-Wall -Wextra -Wpedantic -Werror`
  as every other target, registered as `add_test(NAME
  aetherfield_dsp_param_tests COMMAND aetherfield_dsp_param_tests)` — the
  exact structure docs/phases/phase1-s2-verification-plan.md already specified.

None of the above is applied by this document.

## Ownership

Terra designs this specification: translating ADR-004 (b)-(d)'s already-
decided formulas, smoothing contract and transport design into a buildable
interface, per roadmap row 4. It decides no new mapping, smoothing law, or
transport mechanism — every one is cited back to ADR-004. The judgment
calls this plan makes are flagged explicitly: the `FeedbackDelayNetwork`
extension (three additive methods, justified via the same non-breaking-
extraction argument as S1's `peek()`/`push()`), the single-ramped-`cᵢ`
setter (required by ADR-004 (c)'s own measured drift finding, not a new
choice), and the `D_max_fixture = 48 dB` test value (explicitly not a
product/perceptual decision, and explicitly low-stakes because ADR-004
already proves every `D >= 0` is stable). `T60_min`/`T60_max` are pure
arithmetic on ADR-004's own formulas against the ADR-005 fixture, not new
decisions. Luna independently validates this document against ADR-004/
ADR-005 and phase1-s2-verification-plan.md on disk before any
implementation is proposed for authorization.

Actual test implementation — writing `tests/ParameterTransitionTests.cpp`,
the `ParameterAutomation` source, `FeedbackDelayNetwork`'s three new
methods, and the corresponding `CMakeLists.txt` changes — is reserved for
whenever this implementation is separately authorized, exactly as this
document's Non-goals section states.
