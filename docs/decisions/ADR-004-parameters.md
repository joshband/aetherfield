---
id: "ADR-004"
status: accepted
implementation: "parameter-transitions-implemented-pt; modulation-deferred"
review: current
review_document: null
depends_on: [ADR-002, ADR-003]
---

# ADR-004 — Initial parameter set: units, mappings, smoothing, transport, and the automation/modulation boundary

## Review summary

- **Decision:** define Mix, Decay, and Damp semantics, mappings, and automation boundaries.
- **Why:** render-thread parameter changes need exact endpoints and controlled transitions.
- **Consequence:** PT implements and verifies the three parameter-transition contracts.
- **Uncertainty:** modulation, stereo behavior, and broader product controls remain deferred.

<!-- Original ADR record -->

## ADR-004 — Initial parameter set: units, mappings, smoothing, transport, and the automation/modulation boundary

**Status:** Accepted

This ADR decides the *initial public parameter surface* and the contracts that govern how a control value becomes a coefficient on the render thread. It decides no topology, no line count `N`, no delay lengths `mᵢ`, no `T60` endpoint values, no filter form beyond ADR-003's, no stereo strategy, and **no modulation**. It authorizes no implementation and no source file. Every quantitative statement is a proof on stated assumptions, arithmetic on clearly labelled *assumed* values, or a literature/platform citation — never a measurement, because Aetherfield has still measured nothing about reverb (testing.md).

**No sonic claim is made anywhere in this ADR.** Mapping curves, the smoothing time constant and the crossfade law are structural and stability contracts. They are not validated as sounding good, and choosing them is not tuning them. Perceptual acceptance belongs to testing.md's **Sonic acceptance** gate and to nothing in this document. ADR-002's warning applies unchanged: "the math is clean, so it will sound good" is the same error as "it seems stable."

### Context

Roadmap Phase 1 row 3 requires "initial parameter semantics, smoothing and automation boundaries," with the acceptance bar "units/ranges/mapping and transition contracts; candidate controls accepted or deferred with reasons." Charter §14 supplies a candidate list of thirteen controls, states explicitly that they are "candidates, not requirements," instructs the architecture team to determine which are justified, requires every continuous parameter to have a meaningful range, meaningful units, a perceptually appropriate mapping, a smoothing strategy, a realtime-safe implementation, automation behavior and extreme-value testing, and closes with a binding sentence: "Automation must not introduce clicks or destabilize feedback structures."

Three prior decisions constrain this one, and one of them deliberately enables it:

- **ADR-002** fixed the topology and proved strict stability for the *time-invariant* network via `‖ΓA‖₂ ≤ maxᵢ|gᵢ| < 1`. It deferred `N` and the delay lengths, required parameter transport to be lock-free and non-blocking, required per-sample work to be bounded and data-independent, and **authorized no modulation of any kind**.
- **ADR-003** fixed the damping filter `Hᵢ(z) = (1−aᵢ)/(1−aᵢz⁻¹)` and its coefficient derivation from `T60₀` and `T60_π`, made `T60_π ≤ T60₀` a binding control-boundary constraint, placed all parameter validation and clamping at the control boundary, and explicitly handed "the values of `T60₀`/`T60_π` ranges" to this ADR. Its section (e) restates ADR-002's modulation prohibition in full and defines the only two admissible future paths (A: unitary matrix modulation with bars A1–A4; B: empirical, with bars B1–B5).
- **ADR-003 section (c)** recorded, deliberately, that *stability does not depend on coefficient-set atomicity*: any combination of individually valid `gᵢ` and `aᵢ` satisfies both bounds, so a torn or interleaved coefficient update is a smoothness concern, not a stability hazard. That result is what frees the transport decision below from having to buy whole-set atomicity it does not need.
- **docs/phases/phase1-s1-plan.md** fixes the render-thread contract style — `prepare` off the render thread and the only allocating operation; `reset` and `process` noexcept, allocation-free and bounded; caller-owned signal buffers — which any transport mechanism decided here must fit without modification.

Sol decides; Astra accepts; Terra implements when separately authorized; Luna verifies.

### Alternatives

#### (a) Candidate-control triage (charter §14)

Judged on one question only: *can the accepted architecture realize this control today, from decisions already made?* Nothing below is a judgment about whether the control is a good idea.

| Charter §14 candidate | Reaches which internal quantity | Blocked by | Disposition |
|---|---|---|---|
| Mix | two output gains, entirely outside the feedback loop | nothing | **ACCEPT** |
| Decay | `T60₀` → `γ₀` → `gᵢ` (memoryless, in loop) | nothing structural; endpoint *values* depend on the deferred delay set | **ACCEPT** (endpoints derived, not chosen) |
| High Damp | `T60_π` → `βᵢ` → `aᵢ` (filter coefficient, in loop) | nothing structural | **ACCEPT** (as **Damp**) |
| Low Damp | would require a filter that attenuates *below* DC | ADR-003 (a): the one-pole is high-frequency-only; accepting this reopens the filter-form decision | **DEFER** |
| Size | delay lengths `mᵢ` | ADR-002 defers `N` and `mᵢ`; and a swept `mᵢ` is delay-length modulation, barred by ADR-003 (e) Path B | **DEFER** |
| Pre-delay | a pre-delay stage that no accepted ADR establishes | ADR-002 decided the late network only; the stage itself is undecided architecture | **DEFER** |
| Diffusion | input/output diffusion stages | explicit ADR-002 scope exclusion | **DEFER** |
| Mod Depth | modulation depth | ADR-002/ADR-003 (e): no modulation is authorized | **DEFER** |
| Mod Rate | modulation rate | same | **DEFER** |
| Width | stereo tap/decorrelation design | explicit ADR-002 scope exclusion | **DEFER** |
| Freeze | unity loop gain / an energy-preserving state | ADR-002's mandatory `γ_max` clamp deliberately excludes unity loop gain; charter §17 requires a designed state, not `feedback = 1.0` | **DEFER** |
| Bloom | time-varying injection/diffusion | charter §16; roadmap V2/RESEARCH | **DEFER** |
| Texture | coordinated macro over several undecided mechanisms | charter §15; roadmap V3/RESEARCH | **DEFER** |

#### (b) Enforcing `T60_π ≤ T60₀`

| Criterion | (b1) independent `T60_π` control, clamped at the boundary | (b2) independent `T60_π` control, rejected on violation | (b3) couple the controls: expose the *relationship*, derive `T60_π` |
|---|---|---|---|
| Can the constraint be violated? | Yes, then repaired | Yes, then refused | **No — arithmetically unreachable** |
| Runtime branching | one comparison and select per update | one comparison plus a rejection path | **none** |
| Behaviour when Decay moves | `T60_π` silently re-clamps; Damp's meaning drifts with Decay | a previously valid `T60_π` becomes invalid; the host's automation is refused | Damp's meaning is invariant under Decay by construction |
| Survives a torn update (ADR-003 (c)) | the pair must be read atomically or the clamp is computed from mismatched halves | same | **Yes** — the invariant is a property of the formula, not of the pair |
| Discoverability of the limit by the user | a control that stops responding | a control that rejects input | a control whose full travel is always legal |
| §43 simplicity | moderate | worst | **best** |

#### (c) Smoothing mechanism

| Criterion | (c1) one-pole exponential smoother | (c2) fixed-length linear ramp, target reached exactly | (c3) per-block step, no intra-block ramp | (c4) crossfade between two coefficient sets |
|---|---|---|---|---|
| Reaches the endpoint exactly | **No** — asymptotic; `aᵢ = 0` and `wet = 0` are never attained | **Yes**, by assignment at the last ramp sample | Yes, immediately | Yes |
| Stays inside the valid coefficient interval | Yes (convex) | **Yes** (convex; a straight line between two valid values) | Yes | Yes |
| Behaviour at a rapid retarget | absorbs it smoothly; no overshoot | restarts from the current value; no overshoot, slope break only | none — it *is* the discontinuity | doubles the in-flight transitions |
| Bounded, data-independent per-sample work | 1 multiply-add + 1 state per coefficient | **1 add + 1 state per coefficient** | none | 2× the network, or 2× the coefficient set |
| Endpoint-exactness needs a branch | yes (snap-to-target epsilon) | no (the ramp counter is per-block bookkeeping) | no | no |
| Zipper noise | avoided | avoided | **this is the textbook cause** | avoided |
| Cost of the mechanism itself | low | **low** | zero | high; ADR-002's cost floor doubles |
| What it is actually for | continuous controls | continuous controls | nothing here | *discontinuous* changes — delay lengths, a topology switch |

#### (d) Lock-free control→render transport

| Criterion | (d1) per-coefficient atomic array + generation counter | (d2) double/triple-buffered coefficient block, published by index | (d3) lock-free SPSC queue of timestamped events | (d4) mutex-protected block | (d5) plain shared floats |
|---|---|---|---|---|---|
| Charter §19 compliant | Yes | Yes | Yes | **No** — blocking synchronization | **No** — data race, undefined behaviour |
| Whole-set atomicity | No (per-coefficient only) | Yes | Yes | Yes | No |
| Is whole-set atomicity needed? | **ADR-003 (c): not for stability. (b3) above: not for the `T60_π ≤ T60₀` invariant either** | — | — | — | — |
| Render-thread work per block | one acquire load; `2N+2` relaxed loads only when the generation changed | one acquire load plus a bounded copy | drain a queue; split the render loop at event boundaries | lock/unlock | — |
| Wait-free on both sides | **Yes** | Yes with triple buffering; a naive double buffer has a writer/reader overlap window | Yes, with a bounded queue and a stated overflow policy | No | — |
| Buffer-lifetime reasoning required | **None** | Yes — which buffer the reader may still be touching | Yes — queue capacity and drop policy | None | None |
| Sample-accurate automation timing | No — quantized to block starts | No | **Yes** — this is what AUv3's `eventSampleTime` provides | No | No |
| Data-independent render work (ADR-002) | **Yes** | Yes | No — the loop is split by event count | Yes | Yes |

### Decision

#### (a) Accepted controls: three

**The initial public parameter surface is `Mix`, `Decay`, `Damp`.** Every other charter §14 candidate is **DEFERRED**, each for the reason recorded in Alternatives (a) and expanded in the Rationale. This satisfies §14's "keep the initial public parameter surface intentionally small" not by taste but by construction: these three are exactly the controls the accepted architecture can realize from decisions already made. The other ten each require an architectural decision that no accepted ADR has taken, and taking it inside a parameter ADR would be deciding topology by the back door.

Two of the deferrals are load-bearing and must not be read as mere sequencing:

- **Size is blocked twice over.** Its range cannot be stated without `N` and the delay-length specification, both deferred by ADR-002; and a *continuously swept* Size makes the delay bank time-varying, which is precisely the operation ADR-003 (e) Path B bars. A preparation-time-only Size (re-derive `mᵢ`, full reset, tail discarded — the sample-rate path of ADR-003 (d)) is structurally available today, and is **rejected** here as a parameter: charter §33 requires "useful Size/Decay relationships" *and* "robust automation," and a control that cuts the tail whenever it moves delivers neither. Size is the leading candidate for the next parameter ADR, not a gap to be papered over.
- **Mod Depth and Mod Rate are not deferred for scheduling reasons.** They have no internal quantity to reach. ADR-002 and ADR-003 (e) authorize no modulation, so a modulation control would be a control over a mechanism that does not exist and is not permitted to exist yet.

#### (b) Units, ranges, mappings, endpoints

Every accepted control has **two layers**: a canonical **normalized automation value** in `[0, 1]`, which is what a host automates and what is transported, and a **displayed engineering value** derived from it. This split is what allows a control whose engineering endpoints are legitimately deferred to still have a fully specified contract today.

**All three controls are validated and clamped on the control thread, before derivation, per ADR-003 (c): a non-finite control value is *rejected*, not clamped; a finite out-of-range value is clamped to `[0, 1]`. A rejected value leaves the published coefficient set unchanged. The render thread is never where an invalid value is discovered.**

**Decay** — normalized `d ∈ [0, 1]`; displayed in **seconds**.

> `T60₀(d) = T60_min · (T60_max / T60_min)^d`, with `T60₀(0) ≡ T60_min` and `T60₀(1) ≡ T60_max` **assigned exactly**, not evaluated.

Mapping: **exponential (log-linear in seconds)**. Justification: the just-noticeable difference in reverberation time is approximately a constant *percentage*, not a constant number of seconds — Schlecht and Habets report roughly 4 % for noise bands, 5–12 % for impulse responses and 3–9 % for speech (cited in ADR-003, NS-6). A law that gives equal *fractional* change per unit of control travel therefore gives approximately uniform perceptual resolution across the range, which is what §14's "perceptually appropriate mapping" asks for. A linear-in-seconds law would waste most of its travel on differences above the JND at the long end and compress everything audible into the first few percent.

`T60_min` and `T60_max` are **DEFERRED as values and DECIDED as derivations.** They are not chosen; they are computed once the delay set exists:

- `T60_max` is the largest `T60₀` satisfying ADR-003's margin rule `1 − γ₀^{m_min} ≥ δ_margin`, `δ_margin ≥ 1e-3`. This *is* ADR-002's mandatory `γ_max` clamp. It depends on `m_min` and `f_s`, both unavailable today.
- `T60_min` is the smallest `T60₀` for which `γ₀^{m_max}` remains a normal `float` and the network is still a decaying tail rather than a diffuser. It depends on `m_max`.

Inventing numbers for these now would either overstate the achievable decay or silently fix the delay set. Endpoint exactness matters for the same reason: `d = 1` must produce `T60_max` itself, so that the `δ_margin` guarantee is satisfied *by construction* at the extreme rather than by a floating-point evaluation that might land a fraction beyond it.

**Damp** — normalized `h ∈ [0, 1]`; displayed as **decibels of excess high-frequency attenuation, `D`**, and optionally also as the derived `T60_π` in seconds.

> `D(h) = D_max · h` (dB), and `T60_π = T60₀ / (1 + D(h)/60)`.

`D` is defined as *the additional attenuation, in dB, that Nyquist receives over the window in which DC decays by 60 dB.* Mapping: **linear in dB**. The `T60_π ≤ T60₀` constraint is satisfied **structurally**: `D ≥ 0` makes `1 + D/60 ≥ 1`, so `T60_π ≤ T60₀` is arithmetically unreachable to violate. There is no clamp, no comparison, no rejection path and no requirement that Decay and Damp be read atomically together — alternative (b3), chosen for exactly the reason §43 states.

Substituting into ADR-003's derivation collapses it:

> `1/T60_π − 1/T60₀ = (D/60)/T60₀`, hence `βᵢ = 10^(−mᵢ·D/(20·f_s·T60₀))`, hence **`20·log₁₀ βᵢ = −mᵢ·D/(f_s·T60₀)` dB per circulation.**

A line of length `mᵢ` circulates `f_s·T60₀/mᵢ` times in `T60₀` seconds, so the total excess attenuation over that window is exactly `D` dB **for every line, at every delay length, at every sample rate, and at every Decay setting**. `D` is therefore a genuinely homogeneous quantity in Jot's sense, and Damp's meaning does not drift when Decay is changed or when the host changes rate — which directly addresses ADR-002's warning that a coefficient cached across a rate change silently changes the decay.

Endpoints: at `h = 0`, `D = 0`, `βᵢ = 1`, `aᵢ = 0` and `Hᵢ(z) = 1` **exactly** — ADR-003's "damping bypassed is a coefficient value, not a code path" is reachable bit-exactly at a control endpoint. At `h = 1` the realized damping may be limited by ADR-003's `a_max = 0.999` clamp on the longest lines first (largest `mᵢ` → smallest `βᵢ` → largest `aᵢ`), which introduces a mild inhomogeneity at the extreme; per ADR-003 the clamp moves toward *less* damping, so it cannot compromise the bound. The arithmetic in Evidence shows the clamp does not bind until `D` is in the hundreds of dB.

`D_max` is **DEFERRED, with a stated reason that is different in kind from the Decay endpoints**: no structural constraint determines it. Every `D ≥ 0` is stable, bounded-real, and moves the network in the safe direction. `D_max` is therefore a purely perceptual choice and belongs to testing.md's **Sonic acceptance** gate, not to this ADR. It is a single preparation-time constant; nothing else in the design depends on its value.

**Mix** — normalized `m ∈ [0, 1]`; displayed as a **percentage**.

> `dry(m) = cos(π·m/2)`, `wet(m) = sin(π·m/2)`, with `dry(0) ≡ 1`, `wet(0) ≡ 0`, `dry(1) ≡ 0`, `wet(1) ≡ 1` **assigned exactly**.

Mapping: **equal-power (sine/cosine) crossfade**, because `dry² + wet² = 1` at every `m`. Justification: the wet signal of a diffuse reverberant tail is substantially decorrelated from the dry input, so the two sum in *power*, not in amplitude; a linear law (`1−m`, `m`) sums to a power of `0.5` at the midpoint, a 3.01 dB dip, which is the standard constant-power argument. The honest limitation is that the assumption of decorrelation is weakest for the earliest wet energy, and pre-delay and input diffusion — the stages that would guarantee it — are deferred above. Endpoint assignment is what makes `m = 0` a bit-exact dry bypass and `m = 1` a bit-exactly dry-free full-wet insert, both of which are testable.

#### (c) Smoothing and transition contracts

**Select (c2): a fixed-length linear ramp in coefficient space, reaching its target exactly.**

**What is smoothed.** Not control values — **coefficients**. The derivation from a control value to a coefficient involves `pow`/`exp` (`γ₀`, `γ_π`, `γ^mᵢ`) and must stay on the control thread in double precision, per ADR-003. What crosses the thread boundary and what the ramp interpolates is therefore the complete coefficient set: `N` line gains `gᵢ`, `N` damping coefficients, and the two mix gains — `2N + 2` values.

**The damping coefficient that is ramped is `cᵢ = 1 − aᵢ`, not `aᵢ`,** and the render-thread recursion is the algebraically identical one-multiply form

> `wᵢ[n] = wᵢ[n−1] + cᵢ·(vᵢ[n] − wᵢ[n−1])`,  which is  `wᵢ[n] = cᵢ·vᵢ[n] + (1 − cᵢ)·wᵢ[n−1]`.

This realizes ADR-003's `Hᵢ(z)` exactly — same filter, same coefficient derivation, same clamp (`cᵢ ∈ [1 − a_max, 1] = [10⁻³, 1]`) — and honors ADR-003's rule that `1 − aᵢ` is stored rather than formed at render time, because `cᵢ` *is* the stored quantity. The reason for choosing this form over ADR-003's stored `(aᵢ, 1 − aᵢ)` pair is specific to ramping and is measured in Evidence: two independently accumulated `float` ramps that should sum to 1 drift apart by up to `7.5e-5` in `|Hᵢ(1)|` over a ramp, which is two orders of magnitude outside NS-3's `1 + 1e-6` bounded-real tolerance. With a single ramped coefficient there is no second value to drift against.

**Ramp length.** `L = max(1, round(τ_ramp · f_s))`, computed at `prepare`, so the ramp is a fixed *time* and is therefore rate-independent. **`τ_ramp = 20 ms` is a fixed implementation constant, not a control.** It is not exposed, not automatable, and not tunable at runtime at this stage. Its stated reasons are structural — it exceeds the block period an AUv3 host typically requests (≤ 512 frames, ≈ 10.7 ms at 48 kHz), so block-boundary quantization of the target is subsumed by the ramp rather than being an independent artifact; it is orders of magnitude shorter than the shortest decay it modulates, so it is not itself heard as an envelope; and it is the quantity that section (d)'s slow-variation argument constrains, where longer means more margin. **None of these is a claim that 20 ms sounds right.**

**Contract.**

1. Per-sample work is **one add per smoothed coefficient, unconditional** — `2N + 2` adds. The increment is zero when settled, so there is no branch on whether a ramp is in flight, and no branch anywhere depends on a signal value. Any bookkeeping that depends on the ramp counter is at most per-block.
2. The target is reached **exactly, by assignment**, no later than `L` samples after the render thread consumed it. This is what makes the endpoint guarantees in (b) real rather than asymptotic, and it is why (c1) was rejected.
3. **Retargeting mid-transition** starts a fresh `L`-sample ramp from the *current* value. The coefficient is continuous; only its slope changes. It cannot overshoot, because the path is a straight line to a target that is itself inside the valid interval.
4. **The fastest possible change rate** — a new target every block, or every sample — is absorbed, not amplified. The mechanism is a rate limiter: a coefficient moves by at most `(target − current)/L` per sample regardless of how fast targets arrive, and per-sample cost is unchanged, because a new target rewrites an increment rather than adding work.
5. **`reset()` snaps every smoother to its current target and cancels any in-flight ramp.** A coefficient discontinuity requires a signal to modulate, and `reset()` has just zeroed every signal state, so there is none. This keeps post-reset behaviour independent of call history (ADR-003 (d) rule 6) and preserves the silence-in/silence-out equality bit-exactly (rule 7) — the canary would otherwise be broken by a smoother mid-ramp. `reset()` still does not touch targets or configuration (rule 5).
6. **Why the ramp cannot leave the valid set.** Both `gᵢ ∈ (0, g_max]` and `cᵢ ∈ [10⁻³, 1]` are convex intervals, and a linear ramp between two points of a convex set stays in that set at every intermediate sample. Every sample of every ramp is therefore an *individually valid* coefficient combination, and ADR-003 (c) applies verbatim at every one of them: `‖ΓA‖₂ ≤ maxᵢ gᵢ < 1` and `sup_ω|Hᵢ| ≤ 1` hold throughout. This is the connection ADR-003 (c) was recorded to supply; it is not re-derived here. What that argument establishes, and what it does not, is section (d)'s subject.

#### (d) Automation — and its categorical distinction from modulation

**Definition.** *Automation* is a host or UI changing the value of an **accepted, static** control over time. It arrives on the control thread; it is validated and clamped there (ADR-003 (c)); the full coefficient set is derived there in double; the set is published through the lock-free transport below; and the render thread realizes it only as the ramp of (c) traverses coefficient space. Nothing else is automation.

> **Automation does not authorize modulation, and nothing in this ADR moves modulation one step closer to authorization.** Delay-length modulation and feedback-matrix modulation remain governed **exclusively** by ADR-002's prohibition and ADR-003 (e)'s Path A (bars A1–A4) and Path B (bars B1–B5). This ADR opens no third path and relaxes no bar.

**Why the distinction is structural, not a matter of degree.** The accepted parameter set **cannot express modulation**, because no accepted control reaches `mᵢ` or `A`. Mix reaches two gains outside the loop; Decay reaches the memoryless per-line gains `gᵢ`; Damp reaches the damping coefficients `cᵢ`. The delay bank and the feedback matrix are untouched by every accepted control — and the delay bank is exactly the element whose time-invariance ADR-002's proof depends on first ("a pure delay preserves ℓ2 norm"), and exactly the element that ADR-003 (e) Path B identifies as the step delay modulation destroys. The three charter candidates that *would* reach `mᵢ` or `A` — Size, Mod Depth, Mod Rate — are all deferred in (a).

**What automation does to the stability argument, stated per control and without overclaiming.**

1. **Mix is outside the feedback loop entirely.** No rate of change can affect stability. Its only hazard is the classic one: a stepped gain multiplying a signal is a first-order discontinuity, i.e. zipper noise, which the ramp exists to prevent.
2. **Decay preserves ADR-002's bound exactly, at any rate of change.** `gᵢ` is a *memoryless* in-loop element. For any time-varying `gᵢ[n]` with `maxᵢ gᵢ[n] ≤ g_max < 1`, the map `x ↦ Γ[n]Ax` has ℓ2-induced gain `≤ g_max` at every `n`, while the delay bank and the damping filters remain time-invariant and non-expansive. The small-gain argument composes causal bounded operators and does not require any of them to be time-invariant, so the closed loop remains a strict contraction. Decay automation is a smoothness question only.
3. **Damp is the one control that makes a filter coefficient time-varying, and the ℓ2 argument does not transfer to it.** This must be stated plainly rather than assumed away. A time-varying one-pole with `aᵢ[n] ∈ [0, 1)` is **not** ℓ2-non-expansive: with a unit impulse, `a[0] = 0` and `a[n] = a` thereafter, the output energy is `1/(1 − a²)`, an amplitude gain of about **22.4 at `a = 0.999`** (computed in Evidence). `sup_ω|Hᵢ| ≤ 1` is a *frozen-coefficient* statement, and ADR-003 (c) is a statement about combinations of coefficients, not about the energy a coefficient *trajectory* can inject. Pretending otherwise would be the same error ADR-002 calls out for modulation.
4. **What does hold unconditionally for Damp: peak non-expansiveness, at any rate.** With `cᵢ[n] ∈ [0, 1]`, `wᵢ[n] = cᵢ[n]·vᵢ[n] + (1 − cᵢ[n])·wᵢ[n−1]` is a **convex combination**, so `|wᵢ[n]| ≤ max(|vᵢ[n]|, |wᵢ[n−1]|)` for every `n` and every trajectory, however fast. No damping coefficient change, at any speed, can make a damping filter produce a value larger than one already present in the network. This is a property of the convex-combination form ADR-003 chose and of the `[0, 1]` clamp; a first-order shelf, whose impulse response changes sign, would not have it. It bounds the *filter*; it does not by itself close the loop, because a normalized Hadamard matrix has `‖A‖∞ = √N`.
5. **What closes the gap, and what it costs.** Every frozen configuration along a ramp is Schur-stable with a uniform margin (point 6 of (c) plus ADR-002's bound), and the whole network with fixed delay lengths is a finite-dimensional linear system `x[n+1] = A[n]x[n]` of dimension `Σmᵢ + N`. Desoer's frozen-time result for slowly varying discrete systems applies directly: if the eigenvalues of every `A[n]` lie in a disc of radius less than 1 and the matrices vary sufficiently slowly, the system is exponentially stable, and an explicit bound on the variation rate exists. **That bound is not computed here and cannot be, because it requires `N` and the delay set.** So `τ_ramp` is the quantity the theorem constrains rather than a cosmetic choice, it is set at the slow end of the responsive range for margin, and the corner is **measured**, not asserted — case PT-4 below.
6. **The transient is bounded in duration.** A ramp is finite, traverses a straight line once per target change, and terminates in a fixed configuration to which ADR-002's and ADR-003's full time-invariant proofs apply verbatim. Modulation, by contrast, is unceasing by design: it lives permanently in the time-varying regime, so its margin must hold uniformly for all time. That is a different and much stronger requirement, and it is one reason the slow-variation argument above must not be read as a back door to modulation. **It is not one.** A future proposal to drive Damp or Decay from an internal LFO would be a *modulation* proposal — a feature that never settles — and would be governed by ADR-002 and ADR-003 (e), not by this ADR. A host is nevertheless free to send such automation from outside, which is precisely why PT-4 exists.
7. **The backstops are unchanged and remain sound under time variation.** ADR-003's denormal cutoff is a memoryless value guard, and its non-finite detector latches, counts and triggers a deterministic full reset. ADR-003 (e) already records that these remain sound when the network is time-varying — and that they prove nothing.

**Transport: select (d1) — a single-writer/single-reader atomic coefficient block with a generation counter.**

At design level, and without prescribing an implementation: a fixed-size array of lock-free atomic `float` coefficient targets (`N` gains `gᵢ`, `N` damping coefficients `cᵢ`, `dry`, `wet`) sized at `prepare`, plus one atomic generation counter. The control thread validates, clamps, derives the full set in double, stores each target, then release-stores an incremented generation. The render thread acquire-loads the generation once per block; if it differs from the one it last consumed, it copies the targets into its smoothers and records the new generation. Lock-free atomicity of the element type is a **compile-time assertion**, not an assumption. No allocation, no locks, no blocking, wait-free on both sides, and bounded data-independent work: one atomic load per block in the common case, `2N + 2` more when something changed.

Why this and not (d2) or (d3). Whole-set atomicity is the only thing (d1) gives up, and two independent results say it is not needed: ADR-003 (c) proved a torn coefficient set is not a stability hazard, and decision (b3) above makes `T60_π ≤ T60₀` a property of the mapping formula rather than of the pair, so tearing cannot break it either. What tearing can produce is a one-block skew between coefficients — bounded by the block period, far inside the 20 ms ramp that every coefficient is traversing anyway. Paying triple-buffer lifetime reasoning (d2) or an event queue and a split render loop (d3) to remove an artifact the ramp already dominates is unearned complexity under §43. **(d4) is rejected outright** as blocking synchronization on the render thread (§19); **(d5) is rejected** as a data race.

**Consequence: automation is applied at block starts, so it is block-quantized.** Renders that contain parameter changes are reproducible only at a fixed block partition; determinism in testing.md's sense (repeated renders byte-identical) holds per partition and is unaffected. This is stated rather than discovered. The named upgrade is (d3): AUv3's `AUParameterEvent` already carries `eventSampleTime` and `rampDurationSampleFrames`, so if host automation fidelity or a large host block size makes block quantization the dominant time granularity, sample-accurate application becomes the successor decision — one that must also re-argue ADR-002's data-independent-work rule, since it splits the render loop at event boundaries. Host-supplied ramp durations are flattened to target changes at this stage; the internal ramp is applied regardless, which lengthens the effective transition slightly. The integration-layer mapping remains deferred with ADR-001's framework choice.

**Named transition cases.** These are the numbers testing.md's existing **Parameter transitions** gate needs. **They add no new gate, and they do not touch the Modulation experiment gate — automation of static parameters is not modulation.**

| ID | Case | Bound / required record |
|---|---|---|
| PT-1 | Endpoint exactness | At `d ∈ {0,1}`, `h ∈ {0,1}`, `m ∈ {0,1}`, after `L` samples the realized coefficients equal the endpoint constants bit-exactly. At `h = 0`: `cᵢ = 1` and the render output is bit-identical to a damping-bypassed reference. At `m = 0` the wet contribution is exactly 0; at `m = 1` the dry contribution is exactly 0 |
| PT-2 | Invariant under every reachable set, including torn ones | Dense sweep of `(d, h)` including deliberately mismatched generations: every derived `βᵢ ∈ (0,1]`, `aᵢ ∈ [0, a_max]`, `cᵢ ∈ [1−a_max, 1]`, `gᵢ ≤ g_max`, all finite, and `T60_π ≤ T60₀` with **no clamp having been applied** |
| PT-3 | Invalid values | Non-finite control value **rejected**, not clamped; finite out-of-range clamped to `[0,1]`; a rejected value leaves the published set and the generation unchanged. No invalid coefficient is ever observable on the render thread |
| PT-4 | Repeated retargeting at the maximum rate — **the corner where the ℓ2 argument is unavailable** | New target every block *and* every sample, alternating between range extremes for `Damp` and `Decay` simultaneously, at `T60₀ = T60_max`, full-scale input, for ≥ 10·`T60_max`, at every supported rate. Required: no non-finite sample; detector counter zero; peak internal magnitude recorded; output growth recorded **relative to an unautomated reference held at the worst endpoint**, never clipped away |
| PT-5 | Changing block partitions | The same automation script under partitions `{1, 13, 64, 512, ragged}`. Outputs are **not** expected identical — targets are applied at block starts — so the required record is a bounded difference against a stated tolerance, no discontinuity and no non-finite value in any partition. Within one partition, repeated renders byte-identical |
| PT-6 | No discontinuity from smoother state reset | After `reset()`, no ramp is in flight and every smoother holds its current target. Silence in yields exactly zero out, bit-for-bit. `reset(); reset()` is indistinguishable from one. A `reset()` issued mid-ramp leaves no residue: the next impulse response is that of the *target* configuration, not of a mixture |
| PT-7 | Signal-transition metrics plus audition | Endpoint-to-endpoint sweep of each control over `L` samples, on a steady full-scale sine and on impulse-plus-silence. Record maximum sample-to-sample output difference and the residual spectrum against a much slower reference sweep, to quantify the slope discontinuity at ramp start and end. Audition per the gate |
| PT-8 | No allocation or blocking on the render path | Zero allocations across any number of parameter changes during `process()`; no locks; the transport's atomic types asserted lock-free at compile time; per-sample instruction and branch count identical between a no-change run and a change-every-sample run |
| PT-9 | Damp is invariant under Decay and sample rate | At every supported rate and ≥ 3 values of `T60₀`, the measured excess attenuation at Nyquist over the `T60₀` window matches `D(h)` within a stated tolerance, confirming the mapping's delay-length, rate and Decay independence |

#### (e) No sonic-quality claim

Stated explicitly, as ADR-002 and ADR-003 do. The exponential Decay law, the dB-linear Damp law, the equal-power Mix law, `τ_ramp = 20 ms`, and the choice of a linear rather than a curved ramp are **structural and stability contracts**. They make transitions bounded, testable, endpoint-exact and provably confined to a valid coefficient set. **They are not validated as sounding good, and nothing here claims they will.** Whether 20 ms is audible as a lag, whether the slope discontinuity at ramp endpoints is audible as a soft click, whether equal-power Mix feels balanced at its midpoint, and what `D_max` should be are all perceptual questions answerable only by testing.md's **Sonic acceptance** gate. PT-7's metrics are inputs to that judgment, not substitutes for it. §45 lists "absence of obvious zipper noise" among the Core Reverb Milestone's expected qualities; this ADR supplies a mechanism intended to achieve that and no evidence that it does.

### Rationale

#### Why three controls, and why the deferrals are not timidity

§14 asks the architecture team to determine which candidates are justified, and §46 asks whether a feature "materially improves Aetherfield's musical identity or engineering foundation." The sharper test available here is narrower and more objective: *does an accepted decision already define the internal quantity this control would move?* For Mix, Decay and Damp the answer is yes, and the mapping falls out of ADR-003's derivation with no new DSP. For the other ten it is no, and in eight cases the missing decision is one ADR-002 explicitly listed as excluded. A parameter ADR that accepted Width would be deciding stereo extraction; one that accepted Diffusion would be deciding the diffusion topology; one that accepted Freeze would be overriding ADR-002's `γ_max` clamp, which that ADR made mandatory precisely so Freeze would need its own energy-preserving design. Those are ADR-002's decisions to reopen, not this one's to assume.

Low Damp is the most tempting exception and is worth naming, because ADR-003 pre-analyzed it: the peak-normalized generalization `Hᵢ(z) = (1 − |aᵢ|)/(1 − aᵢz⁻¹)` extends the same bounded-real guarantee to `aᵢ ∈ (−1, 1)`, and the monotonicity lemma makes the check two points. The escape hatch exists. Taking it would still change ADR-003's decided filter and add a degree of freedom to every line for a control §14 lists as a candidate and nothing yet motivates. ADR-003's own words apply: deciding a filter for a parameter that may not exist is the premature complexity §43 and §46 rule out.

#### Why the Damp control is defined as decibels of excess attenuation

The obvious alternative is to expose `T60_π` directly in seconds, alongside `T60₀`. It fails three ways at once. It admits `T60_π > T60₀`, so ADR-003's binding constraint has to be repaired at runtime by a clamp or a rejection, adding the branch §43 disfavours and putting the repair in the path of every update. It makes Damp's meaning drift under Decay: a fixed `T60_π` of 1 s is gentle damping at `T60₀ = 1.2 s` and severe damping at `T60₀ = 30 s`, so moving one control silently changes what the other one does. And it requires the two to be read together, reintroducing exactly the set-atomicity obligation ADR-003 (c) worked to remove.

Defining the control as the *relationship* fixes all three simultaneously, and the algebra rewards it. Because `1/T60_π − 1/T60₀ = (D/60)/T60₀`, the excess attenuation per circulation is `−mᵢ·D/(f_s·T60₀)` dB, which multiplied by the `f_s·T60₀/mᵢ` circulations in the decay window gives exactly `D` dB — independent of `mᵢ`, of `f_s`, and of `T60₀`. Verified numerically across twelve `(mᵢ, T60₀, D)` combinations in Evidence, to twelve significant figures. That is not a convenience: it is the same homogeneity property Jot's design exists to provide, expressed at the control surface instead of only inside the coefficient derivation, and it is what lets Damp's displayed value mean the same thing at every Decay setting and every sample rate.

#### Why coefficient-space interpolation, and why one coefficient per filter

Interpolating in *control* space would require `pow` on the render thread — excluded by ADR-003, which derives coefficients at preparation time in double, and by ADR-002's bounded-work rule. So the ramp necessarily lives in coefficient space, and the consequence must be stated honestly: the realized `T60₀` during a Decay ramp is not the exponential interpolant between the endpoint `T60` values, because a linear ramp in `gᵢ` is not a linear ramp in `T60`. Over 20 ms against a decay measured in seconds this is not a meaningful deviation, but it is a deviation, and PT-7 measures the transition rather than assuming it.

The choice to ramp a single coefficient per damping filter is the one place where doing the arithmetic changed the answer. ADR-003 stores the pair `(aᵢ, 1 − aᵢ)`, which is correct and well-motivated for a *static* coefficient: `1 − aᵢ` is derived once in double and never formed at render time. Ramping, however, would require either two independently accumulated ramps or a per-sample subtraction. Two accumulated `float` ramps do not stay consistent: over a ramp of 480–2400 samples they drift enough that `|Hᵢ(1)| = (1−aᵢ)/(1−aᵢ)` departs from 1 by up to `7.5e-5`, which exceeds NS-3's `1 + 1e-6` tolerance by roughly two orders of magnitude. That is not a stability failure — `δ_margin ≥ 1e-3` absorbs it — but it would make a measured bounded-realness check fail for a purely arithmetic reason, and a stability check that fails for a reason unrelated to stability is worse than no check. The one-multiply form removes the problem by removing the second coefficient: the recursion needs only `cᵢ`, `aᵢ` is never formed, and `cᵢ = 1` gives bit-exact pass-through, which is what makes PT-1's bypass assertion an equality rather than a tolerance.

#### Why a linear ramp rather than a curved one, and the limitation that choice carries

A linear ramp is continuous in value but not in slope: it has a corner at both ends, so its spectral splatter rolls off more slowly than a `C¹` curve's would. A raised-cosine or smoothstep ramp would remove those corners at the cost of a multiply per coefficient per sample, a lookup or a second-order generator, and a more complicated retargeting rule. §43 is explicit that clever DSP loses to understandable DSP "unless experimentation demonstrates a meaningful advantage," and no listening evidence exists. So: linear now, with the concern recorded rather than hidden, PT-7 measuring it, and the raised-cosine ramp named as the successor if audition finds ramp-endpoint artifacts. This mirrors ADR-003's treatment of the higher-order attenuation filter as a pre-analyzed upgrade path that is simply not taken yet.

#### Why `reset()` snaps the smoothers

testing.md's gate requires "no discontinuity from smoother state reset," which reads at first like an argument for *preserving* smoother state across a reset. The opposite is correct, and the reason is precise: a coefficient discontinuity is only audible as a discontinuity in a signal, and `reset()` has just zeroed every delay buffer and every filter state, so there is no signal for a coefficient jump to act on. Preserving a mid-ramp coefficient would instead make post-reset behaviour depend on call history, violating ADR-003 (d) rule 6, and would break rule 7's silence-in/silence-out equality as a canary — because an object reset mid-ramp would render a different impulse response from an identically configured object reset while settled. Snapping makes `reset()` mean exactly what ADR-003 says it means: clears state, never configuration, idempotent, history-independent.

#### Why the automation/modulation boundary had to be argued and not merely asserted

It would have been easy, and wrong, to write that a smoothed transition is stable because ADR-003 (c) says any valid coefficient combination is stable. ADR-003 (c) is a statement about *combinations*, and a ramp is a *trajectory*; the gap between the two is exactly the gap ADR-002 identifies for modulation, and the counterexample in (d)(3) shows it is a real gap for the damping coefficient specifically — a time-varying one-pole can have an ℓ2 gain of 22 in amplitude even though every frozen configuration it passes through is non-expansive. Sol's obligation is symmetric: the same scepticism ADR-002 applies to "the math is clean, so it will sound good" applies to "the coefficients are individually valid, so the ramp is safe."

What survives that scepticism is a layered argument rather than a single proof: Mix cannot affect stability at all; Decay provably cannot, at any rate, because `gᵢ` is memoryless and the small-gain composition tolerates time variation there; Damp cannot amplify a peak at any rate, by convexity; and for Damp's energy behaviour the frozen-time result gives exponential stability under sufficiently slow variation, with the rate bound existing but uncomputable until the delay set is decided. The residue is measured at PT-4 rather than asserted. That is the same discipline ADR-003 applied when it proved the finite-time silence bound and left the internal peak bound to NS-7: prove what is provable, measure what is not, and never let the second masquerade as the first.

The frozen-time result deserves one explicit fence, because a reader will notice that it would also apply to a *slowly* modulated delay length, whose frozen configurations are likewise Schur-stable. **It does not follow that slow delay modulation is authorized, and this ADR does not authorize it.** Three reasons, of which the third is decisive. Desoer's variation-rate bound is not computed here, so "sufficiently slow" names no achievable setting. Modulation is unceasing where automation is convergent, so its margin would have to hold uniformly for all time rather than for a bounded window ending in a proved-stable configuration. And most importantly, modulation is not this ADR's decision to make: ADR-002 prohibits it and ADR-003 (e) defines the only two admissible routes with their bars. A future modulation proposal invoking a slow-variation argument would be making a new argument inside Path B or a new analytic path, in its own ADR, and would still have to satisfy B1–B5 or A1–A4 in full.

### Evidence

No Aetherfield measurement supports this decision. The four computations below were performed during the drafting of this ADR and are arithmetic on stated assumptions, not measurements of any Aetherfield code, of which none exists. Assumed values are labelled; `N`, the delay lengths and the `T60` endpoints remain deferred and nothing here fixes them.

- **The Damp mapping is exactly homogeneous.** For assumed `f_s = 48 kHz` and twelve `(mᵢ, T60₀, D)` combinations spanning `mᵢ ∈ {960, 2400, 4800}`, `T60₀ ∈ {0.2, 0.5, 4, 30} s` and `D ∈ {0, 6, 12, 24, 48, 240} dB`, the derivation `βᵢ = (γ_π/γ₀)^mᵢ` with `T60_π = T60₀/(1 + D/60)` gave `20·log₁₀ βᵢ = −mᵢ·D/(f_s·T60₀)` to twelve significant figures in every case, and the excess attenuation accumulated over `T60₀` equalled `D` exactly in every case. Worked example: `mᵢ = 2400`, `T60₀ = 4 s`, `D = 12 dB` gives `T60_π = 3.3333 s`, `βᵢ = 0.982879`, `−0.1500 dB` per circulation, 80 circulations, `−12.000 dB` total, `aᵢ = 0.008634`, `cᵢ = 0.991366`. The `mᵢ` values are illustrative spans, not a delay-length decision.
- **ADR-003's `a_max = 0.999` clamp does not bind in any plausible Damp range.** At `f_s = 48 kHz` the clamp corresponds to `βᵢ ≈ 5.0e-4`, i.e. `−66.0 dB` per circulation. Across assumed `mᵢ ∈ {960, 2400, 4800, 9600}` and `T60₀ ∈ {0.5, 4, 30} s`, the smallest `D` at which it binds anywhere is **165 dB** (longest line, shortest decay), and it rises to tens of thousands of dB for short lines and long decays. This confirms ADR-003's statement that the clamp binds only at extreme damping and, when it binds, moves toward less extreme damping.
- **A time-varying one-pole with `aᵢ[n] ∈ [0,1)` is not ℓ2-non-expansive, but is peak-non-expansive.** Simulated with a unit impulse, `a[0] = 0` and `a[n] = a` thereafter: output energy matched the predicted `1/(1 − a²)` at `a ∈ {0.5, 0.9, 0.99, 0.999}` — amplitude gains of `1.155`, `2.294`, `7.089` and **`22.366`** — while the output **peak was exactly 1.000000 in every case**. A separate randomised search over 20 000 trials of 64 samples with `v` and `a[n]` drawn independently and uniformly from `[−1,1]` and `[0,1]` found a maximum observed peak gain of `0.99997`, never exceeding 1, consistent with the convex-combination bound `|w[n]| ≤ max(|v[n]|, |w[n−1]|)`. This is the basis for (d)(3) and (d)(4).
- **Two independently ramped `float` coefficients do not stay consistent; one ramped coefficient has nothing to drift against.** Accumulating `aᵢ` and `1 − aᵢ` as separate `float` ramps over `L ∈ {480, 960, 2400}` samples between endpoint pairs `(0 → 0.999)`, `(0.999 → 0)` and `(0.2 → 0.7)` gave a maximum `|aᵢ + (1−aᵢ) − 1|` of `3.9e-5` and a maximum `||Hᵢ(1)| − 1|` of **`7.5e-5`**, against NS-3's tolerance of `1 + 1e-6`. In the one-multiply form, `cᵢ = 1` reproduces the input bit-exactly in `float32`, and the DC fixed point is the input value to within `1.5` ulp at the extreme `cᵢ = 10⁻³`.
- **Equal-power versus linear Mix.** `cos²(πm/2) + sin²(πm/2) = 1` to twelve significant figures at `m ∈ {0, 0.25, 0.5, 0.75, 1}`; the linear law `(1−m, m)` sums to a power of `0.5` at `m = 0.5`, a `3.01 dB` dip. Endpoints require assignment because `cos(π/2)` evaluates to `6.1e-17`, not `0`.
- [Desoer, "Slowly varying discrete system x_{i+1} = A_i x_i," *Electronics Letters* 6(11), May 1970, pp. 339–340](https://digital-library.theiet.org/doi/10.1049/el%3A19700239) — if the eigenvalues of every `A_i` lie in a disc of radius less than 1 and the matrices vary sufficiently slowly, the system is exponentially stable, with an explicit upper bound on the variation rate. This is the frozen-time result relied on in (d)(5). It is cited as the *shape* of the guarantee; its bound is not computed for this network and the theorem is not treated as discharging PT-4.
- [Smith, *Physical Audio Signal Processing* (CCRMA): "Delay-Line Interpolation"](https://ccrma.stanford.edu/~jos/pasp/Delay_Line_Interpolation.html) — "when an audio delay line needs to vary smoothly over time, some form of interpolation between samples is usually required to avoid ``zipper noise'' in the output signal as the delay length changes." Cited twice: for the zipper-noise phenomenon charter §45 names, and as the reason Size and Pre-delay need a resampling-aware transition contract that the ramp of (c) does not provide.
- [Schlecht and Habets, "Accurate Reverberation Time Control in Feedback Delay Networks," Proc. DAFx-17, pp. 337–344](https://dafx17.eca.ed.ac.uk/papers/DAFx17_paper_11.pdf) — the reported `T60` just-noticeable differences (≈ 4 % for noise bands; 5–12 % for impulse responses; 3–9 % for speech) used here as the justification for an exponential rather than linear Decay law. Already load-bearing in ADR-003 (NS-6).
- [Reyes, "Multichannel Intensity Panning" (CCRMA, Stanford)](https://ccrma.stanford.edu/guides/planetccrma/Multichannel_Intensity_Pann.html) — the constant-power law `g₁² + g₂² = K` realized by sine and cosine, "the squares of sine and cosine are equal to 'one' which is always constant." Cited for the crossfade law only; the assumption that dry and wet are decorrelated is Aetherfield's, and is stated as an assumption in (b).
- [Bencina, "Real-time audio programming 101: time waits for nothing"](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing) — the standard statement of the unbounded-execution-time hazard and of lock-free message passing between non-real-time and real-time contexts. Cited for the constraint charter §19 imposes, not for the specific mechanism chosen.
- [Doumler, "Using locks in real-time audio processing, safely"](https://timur.audio/using-locks-in-real-time-audio-processing-safely) — that a single numeric value can be shared between threads with a lock-free atomic on modern platforms, and that lock-freedom should be asserted at compile time rather than assumed, because a platform lacking it substitutes a mutex. This is the basis for (d1)'s compile-time assertion requirement.
- [Apple, `AUParameterEvent` (AudioToolbox)](https://developer.apple.com/documentation/audiotoolbox/auparameterevent) — the fields `eventSampleTime` (the sample time within the current render cycle at which the change occurs), `rampDurationSampleFrames` (frames over which to ramp; `0` means immediate), `parameterAddress` and `value`. Cited to establish that sample-accurate, host-ramped automation is what an AUv3 host can deliver, and therefore what alternative (d3) would be needed to honour. The AUv3 framework choice itself remains **DEFERRED** per ADR-001, and nothing here selects it.

### Consequences

Aetherfield now has a public parameter surface — three controls — whose every mapping terminates in a quantity ADR-003 already derives, and whose binding constraint `T60_π ≤ T60₀` is enforced by the shape of the mapping rather than by a runtime repair. No accepted control can reach a delay length or the feedback matrix, so the parameter set is structurally incapable of expressing modulation; the prohibition is now enforced by what the controls *are*, not only by what the documents say. Damp's displayed value means the same thing at every decay time and every sample rate, which removes one instance of ADR-002's "a cached coefficient silently changes the decay" hazard from the control surface entirely.

The transitions have a contract that is testable rather than aspirational: endpoints are reached exactly and asserted bit-exactly; every intermediate coefficient is individually valid by convexity, so ADR-003 (c) applies at every sample of every ramp; the mechanism rate-limits rather than amplifies the fastest possible automation; and per-sample work is `2N + 2` unconditional adds independent of how fast parameters change, preserving ADR-002's data-independent bound. `reset()` gains one rule — smoothers snap to target — which is what keeps ADR-003 (d)'s silence-in/silence-out canary bit-exact.

The decision creates obligations, and one of them is an admitted gap. The ℓ2 small-gain bound does **not** transfer to a ramping damping coefficient; what covers that case is peak non-expansiveness plus a frozen-time slow-variation result whose rate bound cannot be computed until the delay set is decided. `τ_ramp = 20 ms` is therefore a load-bearing safety quantity rather than a cosmetic one, and PT-4 — maximum decay, both in-loop controls retargeted every sample between extremes, for ten decay times, at every rate — is a mandatory measurement and not a formality. ADR-003's implementation note that `(aᵢ, 1 − aᵢ)` is a stored pair is refined: the ramped and stored quantity is `cᵢ = 1 − aᵢ` alone, in the algebraically identical one-multiply recursion, because two accumulated ramps break NS-3's tolerance for arithmetic reasons unrelated to stability. Automation is applied at block starts, so renders containing parameter changes are reproducible only at a fixed block partition — stated here rather than discovered later. And because the canonical automation quantity is normalized while `T60_min`/`T60_max` are derived from the eventual delay set, an automation curve or a stored preset is only comparable within a fixed delay set: a later change to `N` or the delay lengths is a state-migration obligation, not a free change.

Several things stay deliberately open and must not drift into being treated as settled: `T60_min`, `T60_max` and `D_max` are numbers this ADR refuses to invent — the first two because they are *derived* from decisions not yet made, the third because nothing structural determines it and only listening can. Size, Pre-delay, Diffusion, Width, Low Damp, Freeze, Bloom, Texture, Mod Depth and Mod Rate are all deferred with named blockers. Size is the most consequential: charter §33 requires useful Size/Decay relationships for V0, and delivering it needs either a decided delay set plus a resampling-aware transition contract, or ADR-003 (e) Path B evidence — neither of which exists. **Modulation is not moved one step closer to authorization by this ADR.** ADR-002's prohibition and ADR-003 (e)'s Path A and Path B bars stand exactly as they were, the frozen-time result in (d)(5) opens no third path, and the bar remains unmet.

### Revisit When

`D_max`, `τ_ramp` or the mapping curves are contradicted by testing.md's Sonic acceptance gate — the most likely of these is `τ_ramp`, either as an audible lag on Mix or as too fast for the in-loop coefficients; PT-7 or audition identifies a soft click at ramp endpoints, at which point the raised-cosine or smoothstep ramp becomes the successor decision and the `C¹` concern recorded in the Rationale is the reason; PT-4 shows output growth under maximum-rate retargeting that the unautomated reference does not explain, at which point `τ_ramp` becomes a measured minimum rather than a chosen constant, and a rate limit on the control thread may be required in addition; the delay set and `N` are decided, at which point `T60_min`, `T60_max`, `g_max` and the concrete `δ_margin` become computable and must be recorded, and Size becomes decidable — its transition contract, not its range, being the open question; a Low Damp control is accepted, which reopens ADR-003's filter form under its own "Revisit When"; a Freeze or Bloom requirement arrives, which reopens ADR-002's `γ_max` clamp before it reaches this ADR; host block sizes or automation fidelity make block-quantized application the dominant time granularity, at which point alternative (d3)'s sample-accurate event transport becomes the successor decision and must re-argue ADR-002's data-independent-work rule; a future parameter's invariant genuinely requires whole-coefficient-set atomicity — a delay-length change and Freeze are the two candidates — at which point alternative (d2) becomes necessary and ADR-003 (c)'s relaxation stops being sufficient; the AUv3 framework decision is taken under ADR-001, which fixes how host parameter events, ramps and state serialization map onto this contract; or modulation is proposed, at which point ADR-002's prohibition and ADR-003 (e) govern, this ADR's automation argument confers nothing, and the fixed unautomated baseline must be retained.
