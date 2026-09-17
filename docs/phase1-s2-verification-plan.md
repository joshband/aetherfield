# Phase 1 S2 — Fixed late network and parameter transitions: consolidated verification-case specification

This is a deliverable for roadmap.md Phase 1 row 5 ("Specify deterministic
fixtures, metrics and verification cases," acceptance bar "Commands and
expected bounds defined; listening review remains required — Terra designs;
Luna validates"). It consolidates the two case tables that ADR-003
(decisions.md, section "(c) Numerical bounds and the stress specification,"
cases **NS-1 through NS-11**) and ADR-004 (decisions.md, section
"(d) Automation," cases **PT-1 through PT-9**) already defined, into one
buildable verification-case specification. It targets exactly two of
testing.md's five "PLANNED validation gates after Phase 1" rows: **Fixed
late network** and **Parameter transitions**.

This document transcribes and organizes 20 already-decided test
requirements. It does not invent, tighten, loosen, or interpret away any
bound. Where a case's bound depends on a quantity no accepted ADR has fixed
yet (`N`, the delay lengths `mᵢ`, `T60_min`, `T60_max`, `D_max`), that
dependency is flagged, not resolved.

## Non-goals (read first)

This document is a design/task plan only, per roadmap row 5's acceptance
bar. It does **not** authorize:

- Writing or editing any `.h`/`.cpp`/`CMakeLists.txt` file. Nothing below is
  applied to the build; it is the exact shape a future, separately
  authorized implementation step should follow, in the same sense
  phase1-s1-plan.md's "Build wiring (future, not applied now)" section is.
- Any new architecture, topology, parameter semantics, or numeric bound.
  Every bound quoted in the tables below is transcribed from ADR-003 or
  ADR-004 as written; this document reorganizes and cross-references them,
  it does not re-derive or restate them with different numbers.
- Fixing `N`, the delay-length set `mᵢ`, `T60_min`, `T60_max`, or `D_max`.
  These remain **DEFERRED** exactly as ADR-002, ADR-003 and ADR-004 left
  them. Each case below that names one of these quantities in its bound is
  flagged in its "Depends on (undecided)" column.
- Coverage of testing.md's **Modulation experiment** or **Sonic acceptance**
  gates. See the dedicated section below for why each is out of scope here.
- Any S2 or parameter-transport *implementation* (a `FeedbackDelayNetwork`
  class, a parameter-smoothing/transport class, or their test executables'
  actual source). That is a future, separately authorized step, exactly as
  phase1-s1-plan.md reserved `DelayLine.h`/`.cpp` for separate
  authorization.

## Build-target structure

**Proposal: two new test executables, not one combined binary** —
`aetherfield_dsp_fdn_tests` (future `tests/FdnTests.cpp`) for NS-1..NS-11,
and `aetherfield_dsp_param_tests` (future `tests/ParameterTransitionTests.cpp`)
for PT-1..PT-9. Both would link against the existing `aetherfield_dsp`
STATIC library, carry the same `-Wall -Wextra -Wpedantic -Werror` options
already applied to every target in CMakeLists.txt, and register as their own
CTest cases (`add_test(NAME aetherfield_dsp_fdn_tests COMMAND
aetherfield_dsp_fdn_tests)` and the equivalent for
`aetherfield_dsp_param_tests`) — the identical pattern phase1-s1-plan.md
used to add `aetherfield_dsp_delay_tests` alongside the existing
`aetherfield_dsp_tests` (`GainProcessorTests.cpp`).

**Why two, not one.** The existing precedent is one executable per DSP
*component*, not one executable total: Phase 0 has a single component
(Gain) and a single executable; phase1-s1-plan.md added a second component
(`DelayLine`) and a second, separate executable, rather than merging a
second `main()` into `GainProcessorTests.cpp`. NS-1..NS-11 and PT-1..PT-9
test two distinct components with two distinct failure modes: NS-cases are
static-network numerics (orthogonality, bounded-realness, decay law,
denormal handling — properties of a *time-invariant* configuration, per
ADR-003), while PT-cases are dynamic transition behavior over time
(smoothing, retargeting, transport atomicity — properties of a
*trajectory*, per ADR-004). Keeping them in separate CTest-registered
binaries means a CTest failure report immediately localizes the defect to
the fixed network or to the parameter/transport layer, without reading test
output first — the same diagnostic value the S1/Gain split already
provides. A single combined executable would save one `add_executable`/
`add_test` pair at the cost of that localization, which is not a good trade
under charter §43's preference for the smaller, more legible option only
when it doesn't cost something else real.

**Not applied now.** As with phase1-s1-plan.md's "Build wiring" section,
none of the above is written to CMakeLists.txt or any source file by this
document.

## Dependencies not yet decided (read before the tables)

**Floor dependency, now discharged by ADR-005:** all twenty cases required
an actual S2 fixture — a decided line count `N` and a decided delay-length
set `mᵢ` — to be executable at all. ADR-002 deferred both to a follow-up ADR
(baseline `N = 8`, bracketed by `N = 4` and `N = 16`). **ADR-005
(decisions.md) now decides** `N_fixture = 8` and the concrete co-prime delay
set derived from a 27–81 ms time-domain specification at both 48 kHz and
44.1 kHz. The S2 evaluation fixture's line count and delay lengths are no
longer deferred for the purpose of executing these twenty cases. **The
product's final `N` remains DEFERRED** to S2's measured evidence, per ADR-005.

Three quantities still block a subset of cases and remain genuinely deferred:
`T60_min`, `T60_max` (ADR-004 defers these as *derived* quantities depending
on the decided delay set, now computable), and `D_max` (deferred to testing.md's
Sonic acceptance gate per ADR-004).

The "Depends on (undecided)" column in each table lists quantities **beyond**
the now-fixed `N_fixture` and `mᵢ` that a case's bound formula names explicitly.
"None beyond the delay-set floor" means the case needs nothing else once the
fixture is prepared.

## Gate: Fixed late network (testing.md row 2; ADR-003 §(c), NS-1..NS-11)

| ID | Case | What it tests (ADR-003 §(c)) | Build target | Depends on (undecided) |
|---|---|---|---|---|
| NS-1 | Matrix orthogonality | `‖AᵀA − I‖∞ ≤ 1e-6` in `float`, `≤ 1e-12` in double, computed independently of the construction | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor (ADR-005 fixes `N_fixture = 8`) |
| NS-2 | Bounded-realness, exact | For every `i`: `max(\|Hᵢ(1)\|, \|Hᵢ(−1)\|) ≤ 1`. `\|Hᵢ(1)\| = 1` and `\|Hᵢ(−1)\| = (1−aᵢ)/(1+aᵢ)` | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor — the two-point check is generic over any valid `aᵢ ∈ [0, a_max]`; representative/test `aᵢ` values can exercise it before the real delay set exists, but per-line coverage over the *accepted* `mᵢ` needs the delay set |
| NS-3 | Bounded-realness, falsification | Dense grid of ≥ 2¹⁶ points on `[0, π]`: `sup\|Hᵢ(e^{jω})\| ≤ 1 + 1e-6`. Required *even though* NS-2 is exact, per ADR-002: measured, never assumed | `aetherfield_dsp_fdn_tests` | Same note as NS-2 |
| NS-4 | Margin | Record `ρ = σ_max(ΓA)`, require `ρ ≤ maxᵢ gᵢ·(1 + 1e-6)` and `1 − ρ ≥ δ_margin` | `aetherfield_dsp_fdn_tests` | A `T60₀` target (to form `Γ`); `mᵢ` fixed by ADR-005; `δ_margin` is fixed at `≥ 1e-3` by ADR-003, not deferred |
| NS-5 | Coefficient finiteness | Every stored coefficient finite and inside its stated interval, at every supported rate and at both `T60` extremes | `aetherfield_dsp_fdn_tests` | `T60_min`, `T60_max` (ADR-004 defers both as quantities *derived* from `m_min`/`m_max`, not yet computable) |
| NS-6 | Decay law | Measured energy decay curve vs. `10^(−3n/(f_s·T60₀))` at DC, and realized `T60(ω)` vs. target in ≥ 8 bands; **record the maximum band error in percent** and compare it against the reported `T60` JNDs (4–12 % per Schlecht and Habets, cited in ADR-003) | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor (ADR-005 fixes the delay lengths) |
| NS-7 | Worst-case combination | `T60₀ = T60_max` × minimum damping (`aᵢ = 0`) × full-scale input, ≥ 120 s, across block partitions `{1, 13, 64, 512, ragged}`, at every supported rate. Required: no non-finite sample; detector counter zero; peak internal magnitude recorded; output overshoot reported, never clipped away | `aetherfield_dsp_fdn_tests` | `T60_max` explicitly named in the bound (deferred, ADR-004) |
| NS-8 | Finite-time silence | From a full-scale impulse with zero input thereafter, the network reaches **exactly zero** on every state within `T_silence ≤ (m_max/f_s)·ln(ε/‖s₀‖₂)/ln ρ`. Record measured time-to-silence, and time-to-first-denormal with the cutoff disabled | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor (ADR-005 fixes the delay set, hence `m_max`); `ε = 1e-20` is fixed by ADR-003, not deferred |
| NS-9 | Denormal cost | Cost with cutoff, without cutoff, and with/without `FPCR.FZ` — four measurements. `FPCR.FZ` may only be adopted on this evidence | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor (needs a real `N`-line network to measure realistic per-sample cost) |
| NS-10 | Double-precision reference | Same fixture in double, with a justified tolerance (ADR-002 S2 item 7) | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor |
| NS-11 | Determinism | Repeated renders byte-identical (testing.md). The cutoff keeps this true; anti-denormal noise would have put it at risk | `aetherfield_dsp_fdn_tests` | None beyond the delay-set floor |

## Gate: Parameter transitions (testing.md row 3; ADR-004 §(d), PT-1..PT-9)

| ID | Case | What it tests (ADR-004 §(d)) | Build target | Depends on (undecided) |
|---|---|---|---|---|
| PT-1 | Endpoint exactness | At `d ∈ {0,1}`, `h ∈ {0,1}`, `m ∈ {0,1}`, after `L` samples the realized coefficients equal the endpoint constants bit-exactly. At `h = 0`: `cᵢ = 1` and the render output is bit-identical to a damping-bypassed reference. At `m = 0` the wet contribution is exactly 0; at `m = 1` the dry contribution is exactly 0 | `aetherfield_dsp_param_tests` | `T60_min`, `T60_max`, `D_max` fix what the `d`/`h` endpoints *map to*, though the bit-exactness assertion itself is about coefficient assignment, not the mapped values |
| PT-2 | Invariant under every reachable set, including torn ones | Dense sweep of `(d, h)` including deliberately mismatched generations: every derived `βᵢ ∈ (0,1]`, `aᵢ ∈ [0, a_max]`, `cᵢ ∈ [1−a_max, 1]`, `gᵢ ≤ g_max`, all finite, and `T60_π ≤ T60₀` with **no clamp having been applied** | `aetherfield_dsp_param_tests` | `T60_min`, `T60_max`, `D_max` (bound the swept `(d, h)` range) |
| PT-3 | Invalid values | Non-finite control value **rejected**, not clamped; finite out-of-range clamped to `[0,1]`; a rejected value leaves the published set and the generation unchanged. No invalid coefficient is ever observable on the render thread | `aetherfield_dsp_param_tests` | None beyond the delay-set floor — control values are normalized `[0,1]`; only building the fixture to observe render-thread coefficients needs `N`/`mᵢ` |
| PT-4 | Repeated retargeting at the maximum rate — the corner where the ℓ2 argument is unavailable | New target every block *and* every sample, alternating between range extremes for `Damp` and `Decay` simultaneously, at `T60₀ = T60_max`, full-scale input, for ≥ 10·`T60_max`, at every supported rate. Required: no non-finite sample; detector counter zero; peak internal magnitude recorded; output growth recorded **relative to an unautomated reference held at the worst endpoint**, never clipped away | `aetherfield_dsp_param_tests` | `T60_max` explicitly named in the bound (deferred) |
| PT-5 | Changing block partitions | Same automation script under partitions `{1, 13, 64, 512, ragged}`. Outputs are **not** expected identical — targets apply at block starts — so the required record is a bounded difference against a stated tolerance, no discontinuity and no non-finite value in any partition. Within one partition, repeated renders byte-identical | `aetherfield_dsp_param_tests` | None beyond the delay-set floor |
| PT-6 | No discontinuity from smoother state reset | After `reset()`, no ramp is in flight and every smoother holds its current target. Silence in yields exactly zero out, bit-for-bit. `reset(); reset()` is indistinguishable from one. A `reset()` issued mid-ramp leaves no residue: the next impulse response is that of the *target* configuration, not of a mixture | `aetherfield_dsp_param_tests` | None beyond the delay-set floor |
| PT-7 | Signal-transition metrics plus audition | Endpoint-to-endpoint sweep of each control over `L` samples, on a steady full-scale sine and on impulse-plus-silence. Record maximum sample-to-sample output difference and the residual spectrum against a much slower reference sweep, to quantify the slope discontinuity at ramp start and end. Audition per the gate | `aetherfield_dsp_param_tests` | `D_max` (bounds the Damp sweep range); the audition component additionally belongs partly to **Sonic acceptance** — see below |
| PT-8 | No allocation or blocking on the render path | Zero allocations across any number of parameter changes during `process()`; no locks; the transport's atomic types asserted lock-free at compile time; per-sample instruction and branch count identical between a no-change run and a change-every-sample run | `aetherfield_dsp_param_tests` | None beyond the delay-set floor |
| PT-9 | Damp is invariant under Decay and sample rate | At every supported rate and ≥ 3 values of `T60₀`, the measured excess attenuation at Nyquist over the `T60₀` window matches `D(h)` within a stated tolerance, confirming the mapping's delay-length, rate and Decay independence | `aetherfield_dsp_param_tests` | `T60_min`, `T60_max` explicitly named ("≥ 3 values of `T60₀`" requires the range to sample from) |

## Why Modulation experiment and Sonic acceptance are not covered

**Modulation experiment.** ADR-002 "authorizes no modulation of any kind,"
and ADR-003 §(e) restates the prohibition in full: neither Path A (unitary
matrix modulation, bars A1–A4) nor Path B (empirical evidence, bars B1–B5)
has been satisfied by any accepted ADR, and ADR-004 §(d) repeats it again —
"Automation does not authorize modulation, and nothing in this ADR moves
modulation one step closer to authorization." NS-1..NS-11 verify a
*time-invariant* network and PT-1..PT-9 verify *automation* (a static
control changing value, which ADR-004 shows is structurally incapable of
reaching the delay lengths `mᵢ` or the matrix `A`); both are explicitly
scoped away from a time-varying network. There is no authorized modulation
mechanism for a verification case to target — specifying test cases for a
mechanism that does not exist and is not authorized to exist would itself
be inventing architecture, which is outside a verification-plan
consolidation and outside Terra's translation role.

**Sonic acceptance.** testing.md's own description of this gate requires
"recorded peak/RMS/decay/stereo measurements; listening notes identifying
ringing, onset density, width and unintended pitch movement" — that is,
listening evidence against a rendered musical corpus. No reverb code exists
in this repository yet, and decisions.md repeats at every ADR that
"Aetherfield has still measured nothing about reverb": there is no audio to
listen to and no corpus from which this gate's measurements could be built.
Concretely, `D_max` — the one quantity ADR-004 explicitly ties to this gate
("`D_max` is therefore a purely perceptual choice and belongs to
testing.md's Sonic acceptance gate, not to this ADR") — cannot be fixed
without exactly the listening evidence this document cannot produce.
Specifying acceptance criteria for this gate now would mean inventing a
numeric target with no measurement basis, which this document's own opening
line rules out.

## Ownership

Terra designs this specification: translating ADR-003 §(c)'s NS-1..NS-11
table and ADR-004 §(d)'s PT-1..PT-9 table into one buildable,
gate-traceable case list, per roadmap.md row 5 ("Specify deterministic
fixtures, metrics and verification cases... Terra designs; Luna
validates"). Luna independently validates this document against the actual
ADR-003/ADR-004 text on disk — not against Terra's self-report — the same
audit pattern already applied to ADR-002 and phase1-s1-plan.md in
testing.md's "Phase 1 architecture-stage verification" section: confirming
scope compliance, that no bound was altered, and that every case here
traces to its named ADR source.

Actual test implementation — writing `tests/FdnTests.cpp`,
`tests/ParameterTransitionTests.cpp`, the `FeedbackDelayNetwork` and
parameter-transport classes they would exercise, and the corresponding
`CMakeLists.txt` changes — is reserved for whenever S2 and parameter-layer
implementation are separately authorized. This document does not authorize
that either.
