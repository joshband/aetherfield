---
id: "ADR-006"
status: accepted
implementation: task-1-lifecycle-preparation-task-2a-read-only-taps-task-2b-routing-task-3-recovery-task-4-ds-1-through-ds-13-measured-evidence-and-control-forwarding
review: resolved-by-contract
review_document: "../phases/phase1-ds-review.md"
depends_on: [ADR-002, ADR-003, ADR-004, ADR-005]
---

# ADR-006 — Input diffusion, network injection, output tap design and stereo decorrelation

## Review summary

- **Decision:** accept an input-diffusion, injection, output-tap, and stereo architecture as an **evaluation baseline**.
- **Why:** it supplies a bounded candidate product path around the fixed FDN without changing FDN internals.
- **Implementation:** **DS-B Tasks 1–4 are implemented and measured.** Task 2b routes through input diffusion, normalized FDN injection, pre-advance even/odd taps, output diffusion and Mix without altering FDN internals. Task 3 adds wrapper-owned saturated fault accounting and next-nonempty-block recovery without changing standalone FDN behavior. Task 4 records DS-1…DS-13, including independent recurrence/bracket coverage, per-Mix channel metrics, a propagated whole-chain cessation bound, and per-channel whole-path magnitude response. `DiffusionStereoPath` also exposes owner-authorized Mix/Decay/Damp forwarding for the two offline render tools. The DS-B Sonic acceptance component is closed after three owner listening rounds and Sol review; no architectural revision is warranted. See `docs/testing.md` for the exact evidence. None of this authorizes successor topology/tap/control work, a product signal path, host integration, UI, or modulation.
- **Review status:** **resolved-by-contract, 2026-09-17.** The five algebraic/acceptance corrections below replace the superseded claims in the historical ADR record. They permit a bounded integration plan; they do not authorize implementation or establish product quality.

## Correction note — contracts replacing superseded claims (2026-09-17)

**Current completion note (2026-09-18).** The correction contracts below are
implemented and evidenced by DS-B Task 4, including the propagated C3 bound;
DS-13 and three owner listening rounds additionally close the DS-B Sonic
acceptance component. The dated language below records the contract at the
time it was written. It does not authorize work beyond the implemented
evaluation baseline.

This note is part of ADR-006's current contract. The original record below is
preserved for decision history. Where it conflicts with this note, this note
governs future plans, tests and implementation. The selected topology remains
an evaluation baseline, not a proved stereo or perceptual result.

### C1 — decay and poles

The fixed late network's feedback topology and poles are unchanged by external
input/output allpass stages. Those allpass stages have their own poles, and
fixed taps can change which modes are observable at an output. Therefore no
full-path fitted-decay equality with the bare FDN is implied or accepted.
Retain NS-6 as a network-only regression. A future integration must measure
full-path decay separately, including minimum Decay and high Damp, and record
the fit method, window and fixture; it may not fail or pass by comparing that
result to bare-network NS-6 as though the paths were pole-identical.

### C2 — coherence and correlation

For an ideal fixed linear mono-input path, `L = H_L X` and `R = H_R X` imply
`MSC = |S_LR|^2/(S_LL S_RR) = 1` wherever both spectra are nonzero. Different
allpass phases can reduce time-domain correlation without making that ideal
magnitude-squared coherence zero. DS-7 is diagnostic only. Its reported
coherence must use a Welch estimator with at least two complete segments and
must record the excitation, segment count and length, window, overlap, FFT
length and silent-bin floor. It must separately report normalized zero-lag
cross-correlation and a declared short-lag correlation range. A single
periodogram and a zero-coherence target are prohibited as a width acceptance
criterion.

### C3 — silence proof template

Exact-silence timing is not a cascade claim derived by adding unit-state
section times. For a section whose input has ceased, use its stored float
coefficient `q_j`, a proved bound `S_j` on its state amplitude at cessation,
and `epsilon_float = 1e-20F`. Its conservative integer drain is
`D_j = (k_j + 1)d_j`, where
`k_j = min { k in Z : k >= 1 and q_j^k * S_j < epsilon_float }`.
For `q_j = 0`, use the pure-delay end condition instead. This is a proof
template: a future chain proof must establish and propagate every `S_j` from
input cessation through each downstream stage. It is not measured timing, and
it does not establish the historical whole-path additive timeout. Measured
exact-silence, reset and cutoff-placement fixtures remain required.

### C4 — wrapper-owned fault aggregation and recovery

The future path wrapper, rather than an allpass section or the standalone FDN,
owns the aggregate detector and recovery schedule. Each diffuser's
`Sample::nonFinite` is observed before the value reaches the next stage. The
wrapper snapshots the FDN's saturating count and latch around every FDN call:
it adds a positive count delta once, and independently records a false-to-true
latch transition when a saturated count cannot expose a new delta. The wrapper
uses saturating aggregate accounting and records that a latch-only observation
is not an exact event multiplicity. It must not double-count an upstream
fault merely because the FDN later substitutes its non-finite input.

On any observed fault the wrapper sets `resetPending`; it does not reset in
the current sample or zero-frame call. At the beginning of the next nonempty
block, it resets every diffusion section, the FDN and wrapper fault latches,
then clears `resetPending` before processing that block. Cumulative aggregate
counts survive reset and re-prepare; FDN's standalone count/latch semantics
remain unchanged. Zero-frame calls neither process audio nor consume a pending
reset. Future code must test FDN-count saturation, latch observation and this
block-boundary recovery explicitly.

### C5 — channel metrics and conditional 3.01 dB arithmetic

Disjoint, unit-norm taps alone do not prove equal realized channel power or a
zero cross term for their shared mono drive. The `+3.01 dB` comparison is
conditional on all-lag uncorrelatedness and is not a channel-balance or mono
compatibility acceptance value. DS-8 must record `E[L^2]`, `E[R^2]`,
`E[L R]`, actual sum power and the dry/wet covariance across the Mix sweep.
DS-9 must separately record first nonzero arrival, full-path energy centroid
and the isolated output-diffuser contribution for each channel. The 126-sample
(48 kHz) / 122-sample (44.1 kHz) difference describes only the isolated output
diffuser chains; it is not a whole-path onset, balance or centroid bound. No
perceptual tolerance follows until these measurements are reviewed.

<!-- Original ADR record -->

## ADR-006 — Input diffusion, network injection, output tap design and stereo decorrelation

**Status:** Accepted

This ADR decides the stages that surround the already-implemented late network: the **input diffusion topology**, the **injection vector** by which a diffused mono signal reaches the network, the **output tap design** that produces two channels from `N` internal signals, the **output diffusion / decorrelation stage**, and how a stereo wet signal meets ADR-004's mono dry path at the Mix control. It decides no change to the late network's topology, matrix, damping filter, decay law, denormal mechanism or lifecycle; no new normalized control parameter; no product `N` or sample-rate matrix; and **no modulation**. It authorizes no implementation and no source file. Every quantitative statement below is a proof on stated assumptions, arithmetic over the constants decided here, or a literature citation — **never a measurement**, because Aetherfield has measured nothing about diffusion or stereo (testing.md), and that remains true after this ADR because neither exists.

**No sonic claim is made anywhere in this ADR.** An echo-density count, an allpass magnitude response, an energy-conservation identity and a tap orthogonality are combinatorial, spectral and algebraic quantities. They do not establish that the result will sound smooth, spacious, wide or uncoloured. §20's "it seems stable" prohibition and ADR-002's symmetric warning — "the math is clean, so it will sound good" is the same error — apply unchanged, and the Rationale's closing subsection states plainly what this design is predicted *not* to fix.

### Context

Roadmap Phase 1 row 1's work item is "Compare late-network **and diffusion** approaches; select topology, feedback matrix and delay strategy." ADR-002 satisfied the late-network half and explicitly refused the other half; ADR-005 discharged the line count and delay set. The diffusion half has been open across four ADRs, and every one of them named it as a blocker for something else. Charter §33's V0 chain is **pre-delay → diffusion → late network → frequency-dependent decay → stereo decorrelation → wet tone → mix**, and §11's hypothesis diagram places input diffusion before the network and output diffusion plus stereo shaping after it. §45 requires "smooth diffusion" and a "useful stereo image" of the Core Reverb Milestone.

**What the prior ADRs deferred, quoted rather than paraphrased.** ADR-002 rejected the allpass diffusion tank *as the late network* and then said, in the same sentence: "short allpass sections remain the leading candidate for the *input and output diffusion* stages, which this ADR does not decide." Its Alternatives table recorded that for the accepted FDN, "N internal signals are directly available as decorrelated tap sources (strategy itself deferred)," and that the rejected plate topology's "figure-of-eight structure offers natural left/right taps." Its scope exclusions list "stereo decorrelation strategy or output tap design; input and output diffusion topology." ADR-003 restated the deferral; ADR-004 deferred the **Diffusion** and **Width** controls with the blocker recorded as "explicit ADR-002 scope exclusion," and recorded as an honest limitation of its equal-power Mix law that "the assumption of decorrelation is weakest for the earliest wet energy, and pre-delay and input diffusion — the stages that would guarantee it — are deferred." ADR-005 left "the input injection vector, the output tap design, stereo extraction, the diffusion stages" untouched, and named the deferred diffusion stages as one of exactly three levers for the density shortfall it discovered: the decided fixture meets the `T60`-scaled Schroeder–Logan mode-density criterion only up to `T60₀ ≈ 2.66 s`, and "this is also the strongest argument yet recorded for keeping the deferred diffusion stages on the critical path, since they raise echo density without requiring more modes."

**What exists.** `FeedbackDelayNetwork` (S2) and `ParameterAutomation` (PT) are implemented and independently verified (testing.md). The network's current input path adds each input sample identically and unscaled to every line, and its output is the unweighted sum of the `N` peeked line values. Both are labelled, in the plan that introduced them and in testing.md, as a **test-only convention** — "uniform injection, summed tap — a test convention, not a product decision" (docs/phases/phase1-s2-plan.md) — and "neither survives into any later stage." This ADR is what replaces that convention with a decided one. The already-proven internals — the delay bank, the Hadamard matrix, the per-line gains, the damping filters, the denormal cutoff and the non-finite detector — are not touched by anything decided here.

**Inherited constraints, restated rather than assumed remembered.** ADR-002 fixed the FDN topology and proved strict stability for the time-invariant network via `‖ΓA‖₂ ≤ maxᵢ|gᵢ| < 1`. ADR-003 fixed the one-pole damping filter, the deterministic denormal cutoff `ε = 1e-20` applied at exactly the network's two named recursive memories, the mandatory non-finite detector whose counter survives `reset()`, control-boundary validation, and the `prepare`/`reset`/rate-change contract in which `prepare` takes a **time-domain** specification and never a sample count. ADR-004 fixed the three-control surface (Mix, Decay, Damp), the `2N + 2` coefficient set, the 20 ms linear ramp and the lock-free transport. ADR-005 fixed `N_fixture = 8`, the nearest-prime delay-length rule over `[27 ms, 81 ms]`, and the two fixture rates 48 kHz and 44.1 kHz. **None of that is reopened, weakened or re-argued here.**

**The modulation prohibition is restated in full, as ADR-003 (e), ADR-004 (d) and ADR-005 (d) each restated it.** ADR-002 "authorizes no modulation of any kind"; its stability proof is for a **time-invariant** network and does not survive time-varying delay lengths, because a time-varying delay resamples what it stores and the delay bank stops being norm-preserving. ADR-003 (e) defines the only two admissible future routes — Path A (unitary feedback-matrix modulation, bars A1–A4) and Path B (empirical, bars B1–B5) — and neither bar is met. **This ADR opens no third path and relaxes no bar.** The point needs saying twice here, because the literature this ADR draws on modulates its diffusers: Dattorro's plate modulates the tank's allpass delays, and §15/§16 list "time-varying diffusion" and "nonlinear diffusion" among Texture and Bloom mechanisms. **Every delay length decided below is a constant of one `prepare()`, re-derived only on a re-`prepare()`, never swept, interpolated or modulated at render time, and the structure adopted below imports none of the time variation the cited designs use.**

Sol decides; Astra accepts; Terra implements when separately authorized; Luna verifies.

### Alternatives

#### (a) Input diffusion topology

Judged on one question: *does this stage raise echo density before the late network's first arrival, from a structure whose stability and gain are provable and whose parameters are derivable rather than tabulated?* None of the rejected options is a bad design.

| Criterion | (a0) No input diffusion (baseline) | (a1) Series (cascaded) Schroeder allpass sections | (a2) Nested allpass sections (Gardner) | (a3) Recirculating allpass tank (Dattorro) | (a4) Lowpass-only input conditioning |
|---|---|---|---|---|---|
| Echo density before the network's first arrival at 27 ms | **None.** ADR-005 computed the late network alone crosses Schroeder's ≈1000 echoes/s only between 100 and 150 ms | Arrival count grows as `n^(K−1)`; crosses ≈1000/s at ≈8.4 ms for the decided set (arithmetic, Evidence) | Same polynomial order for the same total delay, with more density per unit of delay memory | Highest per unit cost — but it is a *recirculating* structure, i.e. a second late network | **None.** A one-pole filter adds no arrivals at all |
| Magnitude response | — | **Exactly flat**, `\|H(e^{jω})\| = 1` at every frequency, by construction | Exactly flat (a nesting of allpasses is allpass) | Flat only while lossless; the tank's decay gain colours it | By definition not flat — this is a tone decision, not a diffusion one |
| Stability argument | — | **One inequality, `\|g_ap\| < 1`, independent of the delay length** (poles at `\|z\| = \|g_ap\|^{1/d}`) | Same condition per section, plus the nesting must be shown to preserve it | A bound on a product of loop gains — ADR-002's stated objection to (c), unchanged | Trivial |
| Parameter derivation | — | One shared coefficient from a stated criterion; lengths from ADR-005's existing rule | Same, plus a nesting depth and an inner/outer split with no stated criterion | Dattorro's published tables are sample counts at 29.76 kHz — exactly what ADR-002 excluded | One corner frequency, perceptual |
| Cost per sample | 0 | 2 multiplies, 2 adds, 1 denormal select and one delay line per section | Comparable; more bookkeeping per section | Higher; adds a second set of recursive memories to reset and stress | 1 multiply-add |
| Reset / numerical surface added | none | **One recursive memory per section**, and nothing else | One per section, with a nesting order that must be reset consistently | One per section plus the tank's own loop state | 1 filter state |
| ADR-002's own disposition | the status quo it called insufficient | "short allpass sections remain the leading candidate" | not evaluated | **rejected** as the late network; a tank here re-raises the same objection | not a diffusion mechanism |

#### (b) How the diffused signal reaches the network

| Criterion | (b1) Uniform injection of the fully diffused signal, `b = 1/√N · 1` | (b2) `±1/√N` sign pattern from a Hadamard row | (b3) Per-line injection from different diffusion-chain tap points | (b4) `N` independent diffusion chains, one per line |
|---|---|---|---|---|
| `‖b‖₂` (the quantity ADR-003's internal-gain formula `G = ‖b‖₂/(1 − γ₀^{m_min})` needs) | **1 exactly** | 1 exactly | 1 exactly after a `1/√N` scale (each partial cascade has unit magnitude) | 1 exactly after scaling |
| Every line receives equal input energy | **Yes** | Yes | Yes | Yes |
| Free choices with no stated criterion | **none** | which row (7 equivalent choices at `N = 8`) | which stage output feeds which line (`K_in^N` assignments) | none, but `N` delay sets must be derived |
| Requires a change to the shipped `FeedbackDelayNetwork` | **No** — `processSample(input)` already adds its argument to every line; the caller pre-scales | No — needs a per-line sign, i.e. a new method | Yes — a per-line input vector | Yes |
| Density of what each line receives | the **fully** diffused signal (all `K_in` stages) | the fully diffused signal | lines fed from early taps get a *less* scattered signal | fully diffused, independently |
| Added cost | **1 multiply per sample** | `N` sign flips | none | `N ×` the diffusion cost and memory |

#### (c) Output tap design and stereo extraction

| Criterion | (c1) Two disjoint, delay-interleaved line groups | (c2) Two orthogonal `±1` tap vectors over all `N` lines | (c3) Dattorro-style figure-eight taps *inside* the delay lines | (c4) A general dense `2 × N` output mixing matrix | (c5) One mono tap, width from an allpass pair |
|---|---|---|---|---|---|
| `⟨c_L, c_R⟩` | **0** (disjoint support) | 0 (orthogonal rows) | not zero in general; set by the chosen tap positions | 0 only if constrained to be | **irrelevant — the two channels start identical** |
| Equal channel power (`‖c_L‖₂ = ‖c_R‖₂`) | **Yes, exactly** | Yes, exactly | must be arranged by hand | must be constrained | Yes |
| Mono sum `c_L + c_R` | **all `N` lines, coherent, `+3.01 dB`** | supported on only `N/2` lines — half the network drops out of the mono sum | depends on the tap set | depends | the *only* signal, so mono is perfect and width is fake |
| Free choices with no stated criterion | **the sign inside each group** (named, not decided) | which two rows (21 equivalent pairs at `N = 8`) | tap positions and signs: a large tabulated set | every entry | the allpass lengths |
| Extra arrivals contributed | none | none | **yes** — an intra-line tap is a new arrival time at no memory cost | none | none |
| Requires a new `DelayLine` capability | no | no | **yes** — a read at an arbitrary offset, which `DelayLine` does not expose | no | no |
| Cost per sample | `N − 2` adds | `N − 2` adds plus `N` sign flips | as many adds as taps | `2N` multiply-adds | 0 |
| Failure mode if wrong | a channel is built from fewer lines than the other — excluded by the interleave rule | mono sum loses half the lines | a tabulated recipe rather than a derived one | unconstrained | a mono source phase-shifted against itself: the classic non-mono-safe widener |

### Decision

#### (a) Input diffusion: `K_in = 4` series Schroeder allpass sections, one shared coefficient

**Select Alternatives (a1).** The wet path's input stage is a **series cascade of `K_in = 4` single (non-nested, non-recirculating) Schroeder allpass sections**, placed **before** the late network and **outside** every feedback loop. Section `j` realizes

> `A_j(z) = (−g_ap + z^{−d_j}) / (1 − g_ap·z^{−d_j})`,  with the standard one-delay-line structure `v_j[n] = x_j[n] + g_ap·s_j[n]`, `y_j[n] = s_j[n] − g_ap·v_j[n]`, where `s_j[n] = v_j[n − d_j]` is that section's delay output and `v_j[n]` is written back into it.

Two multiplies, two adds, one delay line and **exactly one recursive memory** (the value written into the delay line) per section. `y_j` is the input to section `j+1`; `y_{K_in}` is the diffused signal.

**Section order is not a decision, and is deliberately not made one.** A cascade of LTI sections is commutative, so in exact arithmetic the order of the four sections cannot change the result. It becomes meaningful only under modulation or a nonlinearity, neither of which exists or is authorized. Any implementation order is therefore correct, and an implementation must not be read as having decided anything by choosing one.

**REJECTED for the input stage:** (a0) no diffusion, because ADR-005 already recorded the density shortfall it leaves; (a3) a recirculating allpass tank, for exactly ADR-002's reason — its stability bound is a product of loop gains rather than a single coefficient inequality, and Aetherfield already has one late network; (a4) lowpass-only conditioning, because a one-pole filter adds no arrivals and therefore is not a diffusion mechanism at all (an input bandwidth filter and a wet tone stage remain legitimate, *separate*, **DEFERRED** decisions — see (h)). **(a2) nested allpass sections are DEFERRED, not rejected**: they give more density per unit of delay memory and retain the allpass property, and they are the named upgrade path if DS-6 shows the series cascade's density insufficient. They are not taken now because the nesting depth and the inner/outer length split are free choices for which no criterion is available, which is the premature complexity §43 and §46 rule out.

#### (b) The shared allpass coefficient: `g_ap = (√5 − 1)/2 = 0.6180339887…`

A **single coefficient shared by every diffusion section, input and output**, derived from a stated structural criterion rather than chosen.

A single allpass section's impulse response is `h[0] = −g_ap` and `h[k·d_j] = (1 − g_ap²)·g_ap^{k−1}` for `k ≥ 1`. The criterion is that **the direct term and the first echo be equal in magnitude**, so that no arrival in the section's own impulse response dominates any other by more than the section's decay factor:

> `g_ap = 1 − g_ap²`  ⟺  `g_ap² + g_ap − 1 = 0`  ⟺  `g_ap = (√5 − 1)/2`.

Three exact identities follow and are the checks DS-1 and DS-4 assert: `1 − g_ap² = g_ap` exactly; the section is energy-preserving, `g_ap² + Σ_{k≥1}[(1−g_ap²)g_ap^{k−1}]² = g_ap² + (1 − g_ap²) = 1`; and its ℓ1 norm is `‖h‖₁ = g_ap + (1 + g_ap) = 1 + 2g_ap = √5`.

**Coefficient-domain validation, at the control boundary (ADR-003 (c)'s discipline, not a new mechanism).** `g_ap` must be finite and in `[0, 0.9]`; a violation **fails `prepare`** and changes nothing. The upper guard is numerical, not perceptual: it bounds the worst-case peak (ℓ1) gain of any cascade to a stated finite number and keeps `1 − g_ap²` clear of catastrophic cancellation. `0.9` is not reachable by anything decided here; it exists so that a programming error cannot put a diffusion pole arbitrarily close to the unit circle.

`g_ap` is **DEFERRED as a control and DECIDED as a constant**, exactly as ADR-004 treats `τ_ramp`. It is not exposed, not automatable and not tunable at runtime at this stage. The value sits inside the range the literature uses for input diffusers (Schroeder's typical `0.7`; Dattorro's `0.625` and `0.750`), which is recorded as a consistency check and **not** as the reason for it: the reason is the criterion, and the criterion is structural, not perceptual.

#### (c) Diffusion delay lengths: two time-domain windows, ADR-005's nearest-prime rule, unchanged

The specification is **a pair of times per window and the derivation rule already accepted in ADR-005**; sample counts are outputs of `prepare`, never inputs to it.

> **Input chain.** `K_in = 4` target times geometrically spaced, inclusive of both endpoints, on `[d_min, d_max] = [1 ms, 10 ms]`.
> **Output chains.** `2·K_out = 4` target times geometrically spaced on `[e_min, e_max] = [4 ms, 8 ms]`; channel **L** takes points 0 and 2, channel **R** takes points 1 and 3.
> **Sample counts.** In both windows: the prime nearest to `round(f_s · t)`, tie-break to the **smaller** prime, with ADR-005's strictly-increasing totality guard applied within each window. This is ADR-005 (b)'s rule verbatim; no new mechanism is introduced.

At the ADR-005 fixture rates this yields (arithmetic, reproducible from the rule alone):

| | targets (ms) | `d_j` @ 48 kHz | realized (ms) | `d_j` @ 44.1 kHz | realized (ms) |
|---|---|---|---|---|---|
| input 1 | 1.00000 | **47** | 0.979 | **43** | 0.975 |
| input 2 | 2.15443 | **103** | 2.146 | **97** | 2.200 |
| input 3 | 4.64159 | **223** | 4.646 | **199** | 4.513 |
| input 4 | 10.00000 | **479** | 9.979 | **439** | 9.955 |
| | **Σ** | **852** | 17.75 ms | **778** | 17.64 ms |
| out L-1 | 4.00000 | **191** | 3.979 | **173** | 3.923 |
| out R-1 | 5.03968 | **241** | 5.021 | **223** | 5.057 |
| out L-2 | 6.34960 | **307** | 6.396 | **281** | 6.372 |
| out R-2 | 8.00000 | **383** | 7.979 | **353** | 8.005 |
| | **Σ L** | **498** | 10.375 ms | **454** | 10.295 ms |
| | **Σ R** | **624** | 13.000 ms | **576** | 13.061 ms |

**Properties that are now assertable at `prepare`, and must be asserted rather than trusted:** every diffusion delay is prime; every window's set is strictly increasing; **every diffusion delay is co-prime with every other diffusion delay and with every late-network `mᵢ`**; and `d_max = 10 ms < t_min = 27 ms`, so the diffusion and late-network length ranges cannot overlap and cross-set distinctness is guaranteed by construction rather than by a check that could later stop holding. **Every section satisfies Smith's colorless bound by construction**, since `d_max = 10 ms` and `e_max = 8 ms` are both at or below the ≈10 ms figure below which he records allpass impulse responses as colourless.

**ADR-005's tie-break stops being inert and becomes load-bearing.** It never fired for the late network's delay set; over the four diffusion configurations above it fires **four times** (once at 48 kHz, three times at 44.1 kHz), because short targets fall between prime pairs far more often than long ones. It must therefore be exercised by a test rather than assumed, which is DS-3.

**Bracket.** `K_in ∈ {2, 3, 4, 5}` and `K_out ∈ {1, 2}` are **required runnable configurations** of the same structure over the same evidence list, exactly as ADR-005 made ADR-002's `{4, 8, 16}` bracket runnable rather than a remark. The endpoint times, `g_ap` and the stage counts are the quantities a later measurement may move **without reopening any topology decision in this ADR**.

#### (d) Injection: the fully diffused signal, uniformly, at unit ℓ2 norm

**Select Alternatives (b1).** The diffused signal reaches the network through the **uniform injection vector `b = (1/√N)·1`**: `b` has `‖b‖₂ = 1` exactly, gives every line equal input energy, and requires **no change whatsoever to the shipped `FeedbackDelayNetwork`**, whose `processSample(float)` already adds its argument identically to every line. The caller supplies `y_{K_in}[n] / √N`; the `1/√N` is a single preparation-time constant and one multiply per sample, or zero if it is folded into the last diffusion section's output.

This replaces the test-only convention in exactly two respects and no others: the injected signal is now the **diffused** signal rather than the raw input, and it is **normalized** rather than unscaled. `‖b‖₂ = 1` is what makes ADR-003's internal-gain bound evaluable as `G = 1/(1 − γ₀^{m_min})` with no free quantity left in it.

**The sign pattern of `b` is explicitly NOT decided**, because no criterion available today distinguishes one. (b2) — a `±1/√N` Hadamard row — has identical norm, identical per-line energy and identical cost, and is a pre-analyzed successor requiring one additive method on the network. **(b3), per-line injection from different diffusion-chain tap points, is DEFERRED rather than rejected.** It costs nothing extra and is a real design, but it has `K_in^N` free assignments with no stated criterion, and it would deliberately give some lines a *less* scattered signal, which is the opposite of the stage's purpose. It is named as a zero-cost successor that changes no other decision. **(b4), `N` independent diffusion chains, is REJECTED**: `N ×` the cost and memory of the stage for decorrelation the distinct, mutually prime line lengths already supply.

**Input conditioning moves to the head of the chain.** ADR-003 (c) requires a non-finite input sample to be substituted with `0` and counted. With diffusion upstream, the network's own guard is no longer the first thing an input meets, and a non-finite sample would contaminate a diffusion section's own feedback loop before ever reaching the network. **The substitution must therefore happen at the head of the input diffusion chain.** The network's existing guard stays exactly as implemented, as a redundant second line; it is not weakened, removed or relied upon as the only one.

#### (e) Output taps: two disjoint, delay-interleaved line groups

**Select Alternatives (c1).** The two wet channels are formed from the same per-sample peeked line values `vᵢ[n]` the network already computes, **before** any line is written that sample — the quantity S2's implementation already materializes:

> `y_L[n] = (1/√(N/2)) · Σ_{i even} vᵢ[n]`,  `y_R[n] = (1/√(N/2)) · Σ_{i odd} vᵢ[n]`,  lines indexed in **increasing delay order** as ADR-005 derives them.

At `N = 8` this is `‖c_L‖₂ = ‖c_R‖₂ = 1` exactly, `⟨c_L, c_R⟩ = 0` exactly (disjoint support), `N − 2 = 6` adds and one shared scale factor, and no multiply per line.

**The interleave is the decided part, and it is decided by a criterion.** Each channel's tap set must span the delay range, so that neither channel's direct tap is systematically earlier or later than the other's. Even/odd in delay order satisfies this; a first-half/second-half split would give one channel every short line and the other every long line, and is **REJECTED** on exactly that ground. The **sign inside each group is uniform `+1`**, and this is explicitly a non-decision: no stated criterion distinguishes a sign pattern, and a signed variant (which would add common-mode rejection across the group) is named as a successor.

This requires **one additive capability** on `FeedbackDelayNetwork`: per-sample access to the two group sums, or to the peeked vector they are formed from. It is an addition of exactly the kind `peek()`/`push()` and `processSample()`/`setLineGain()`/`setDampingCoefficientC()` already are — it computes nothing new, changes no internal state, and must leave `process()`, `processSample()` and all eleven NS tests bit-identical. **The exact interface is Terra's to design in a separate, separately authorized task plan**, as every prior ADR→plan→implementation cycle in this project has done.

**REJECTED for now:** (c4) a general `2 × N` mixing matrix, as `2N` free entries with no criterion and `2N` multiplies to realize them; (c5) a mono tap widened by an allpass pair, because it manufactures width from a signal that has none and is the textbook non-mono-safe widener. **(c3), Dattorro-style taps at intermediate positions inside the delay lines, is DEFERRED rather than rejected.** It is the one alternative that adds genuinely new arrival times at no memory cost, and it is the named successor if DS-6 or DS-7 show insufficient onset density or coherence. It is not taken now because `DelayLine` exposes no read at an arbitrary offset, and because the tap positions and signs would be a tabulated recipe rather than a derived rule — which is what ADR-002 and ADR-005 have consistently refused.

**A tap design cannot change the realized decay, and this is provable.** ADR-002's homogeneous decay law makes every line decay at the same rate `γ` regardless of its length. Any fixed linear combination of line signals therefore decays at that same rate, so **no choice of `c_L`, `c_R` can alter the realized `T60(ω)`**. Tap design is a free sonic parameter in exactly the sense ADR-002 established for delay lengths, and NS-6's decay-law result survives this ADR unchanged.

#### (f) Output diffusion and decorrelation: `K_out = 2` allpass sections per channel, distinct lengths per channel

Each tapped channel passes through **its own series cascade of `K_out = 2` allpass sections**, identical in form and coefficient to (a)/(b), with the four delay lengths from (c) assigned so that the two channels share no length. This single stage performs both of §11's separate `POST` (output diffusion) and `WIDTH` (stereo shaping) functions, and the merge is deliberate: an allpass pair with different lengths is simultaneously a diffuser and a decorrelator, and it decorrelates **without changing either channel's magnitude spectrum**, since `|A(e^{jω})| = 1` at every frequency. Width is therefore bought without tonal change — a structural property, not a claim that the width is the right amount.

Consequences that are computable and must be recorded rather than discovered:

- The two chains' total delays differ by `126` samples (`2.63 ms`) at 48 kHz and `122` samples (`2.77 ms`) at 44.1 kHz. **This is not an onset time difference**: an allpass cascade has an instantaneous direct term of magnitude `g_ap^{K_out}`, so both channels respond at the same sample. It is a difference in where each chain's energy centroid sits, and whether it produces an image bias is a measured question (DS-9).
- Under the assumption that `y_L` and `y_R` are uncorrelated, the **mono sum's power is exactly preserved** by the output diffusers, because `|A_L| = |A_R| = 1` and the cross term vanishes. Without that assumption the diffusers do alter the mono sum, which is why DS-8 measures it.
- **ADR-003's named revisit trigger for `ε = 1e-20` is discharged with arithmetic, not with reassurance.** ADR-003 asks whether "a large output gain could in principle lift a truncation artifact" from a stage placed after the network. The output chain's worst-case peak (ℓ1) gain is `(1 + 2g_ap)^{K_out} = 5.000` exactly, and Mix contributes at most 1, so the largest value a truncated `ε` can reach at the output is `5 × 10⁻²⁰ ≈ −386 dBFS`. The concern does not bind.

#### (g) Mix with a stereo wet signal and a mono dry signal

ADR-004's equal-power law `dry(m) = cos(πm/2)`, `wet(m) = sin(πm/2)`, with all four endpoints assigned exactly, is **unchanged**. It is applied as:

> `out_L[n] = dry(m)·x[n] + wet(m)·y_L'[n]`,  `out_R[n] = dry(m)·x[n] + wet(m)·y_R'[n]`

where `y_L'`, `y_R'` are the output-diffused wet channels and `x` is the mono dry input. **The dry signal is summed identically and at full `dry(m)` into both channels** — centred, not attenuated by `1/√2`.

**Why, and what it costs.** With this convention the **equal-power law holds per channel**: each channel's power is `dry(m)²·P_x + wet(m)²·P_wet`, constant across the Mix sweep whenever the wet path is normalized to the input's power. The honest consequence, stated rather than hidden: the dry component is perfectly correlated between channels while the wet component is not, so the **mono sum is dry-biased by 3.01 dB relative to the stereo image** — dry sums coherently (+6.02 dB) and wet incoherently (+3.01 dB). The named alternative is to scale dry by `1/√2` per channel, which fixes the mono sum and makes per-channel power non-constant instead; a compensation curve that fixes both is unearned complexity under §43. Per-channel constancy is selected because stereo is the primary listening case and because it leaves ADR-004's law literally intact. DS-8 measures the mono consequence.

**No new smoothed coefficient is created.** ADR-004's coefficient set stays `2N + 2`: two Mix gains, now applied to three signals instead of two. The transport, the 20 ms ramp, the endpoint-exactness contract and PT-1's bit-exact bypass assertions are untouched, except that PT-1's "the wet contribution is exactly 0 at `m = 0`" and "the dry contribution is exactly 0 at `m = 1`" must now be asserted **in both channels**.

**ADR-004's own recorded limitation is partially — and only partially — discharged.** It noted that the equal-power law's decorrelation premise "is weakest for the earliest wet energy, and pre-delay and input diffusion — the stages that would guarantee it — are deferred." Input diffusion now exists as a decision, which makes the earliest wet energy less like the dry input. **This strengthens the premise; it does not prove it**, and the realized dry/wet correlation is a measured quantity (DS-8), not a settled one. Pre-delay remains deferred.

**Channel configuration of the input is DEFERRED.** Everything above takes a single mono `x`. Whether the product's input is mono or stereo, and how a stereo host input would be reduced or carried through, is an integration and product question entangled with ADR-001's deferred AUv3 framework decision, and deciding it inside a diffusion ADR would be the mandate creep ADR-005 was written to avoid. The structure extends by adding a second input diffusion chain with its own delay set; that is architectural information, not authorization.

#### (h) Stability, and the numerical-safety discipline every new component inherits

Nothing added here is permitted to escape ADR-002's and ADR-003's discipline. Stated per component.

1. **Each allpass section is unconditionally stable for `|g_ap| < 1`, for every delay length.** Its poles satisfy `z^{d_j} = g_ap`, so all `d_j` of them lie at radius `|g_ap|^{1/d_j} < 1`. The bound is independent of `d_j`, which is the same delay-independence that makes ADR-002's orthogonality argument work, and it means the delay-length rule in (c) can be changed freely without touching the stability argument. At `g_ap = 0.618` and `d = 47` the pole radius is `0.9898`; at `d = 479` it is `0.99900`.
2. **Each section, and every cascade of them, has ℓ2-induced gain exactly 1.** `|A_j(e^{jω})| = 1` at every frequency — it is allpass, which is the defining property of the structure and not an approximation to be verified by hope. It follows that **the diffusion stages cannot change the late network's stability margin at all**: they are feed-forward, they sit outside every feedback loop, and even inserted in one they would be non-expansive. `‖ΓA‖₂ ≤ maxᵢ gᵢ < 1` and `1 − ρ ≥ δ_margin` stand verbatim, and ADR-003's `G = ‖b‖₂/(1 − γ₀^{m_min})` is unchanged because the input chain preserves the injected signal's norm exactly.
3. **They are not peak-non-expansive, and this is stated rather than glossed.** `‖h‖₁ = 1 + 2g_ap = √5 ≈ 2.236` per section, so the worst-case peak gain is bounded by `√5^{K_in} = 25.000` for the input chain and `√5^{K_out} = 5.000` for each output chain. These are **bounds on a deliberately constructed worst case, not expected levels**; the realized peak must be **measured and reported, never clipped away** (DS-5). This is exactly ADR-003's split between the provable ℓ2 bound and the measured peak, applied to a new component rather than re-derived.
4. **The output tap and the Mix stage have no state and no poles.** `C` is a fixed `2 × N` real matrix applied to values that already exist, outside every loop, with `‖c_L‖₂ = ‖c_R‖₂ = 1`; Mix's gains are bounded by 1 and outside every loop, which ADR-004 already established. Neither can affect stability by any mechanism.
5. **Denormals.** Each allpass section has **exactly one recursive memory** — the value written into its delay line. ADR-003 (b)'s deterministic cutoff `f_ε(x) = x if |x| ≥ ε else 0`, `ε = 1e-20`, must be applied there and **only** there, expressed as a branchless select, exactly as it is at the network's two named memories. No new mechanism, no new constant, no new tolerance. Because `f_ε` is memoryless with `|f_ε(x)| ≤ |x|`, inserting it cannot raise any gain bound above — and, stated honestly, it also means the realized section is allpass only to within `ε`, since the exact-arithmetic identity is what `|A| = 1` asserts. At `−400 dBFS` that deviation is not a design quantity; DS-2 measures the magnitude response anyway rather than inferring it.
6. **Finite-time exact silence extends, with an additive bound.** With zero input a section's state contracts by `g_ap` per `d_j` samples, so the cutoff drives it to exactly zero within `(d_j/f_s)·ln(ε)/ln(g_ap)` seconds. Summing over a chain gives, at 48 kHz on the decided constants, **≤ 1.699 s for the input chain, ≤ 0.993 s for L and ≤ 1.244 s for R**. The whole wet path's time-to-silence is therefore bounded by the network's own NS-8 bound plus at most `≈ 2.9 s`, and it still terminates exactly rather than asymptotically. DS-10 measures it.
7. **NaN/Inf.** A non-finite value inside a diffusion section is trapped in that section's own feedback loop and is permanent until reset, exactly as in the network — but with one structural difference worth naming: the sections are not mutually coupled, so a non-finite value propagates only *forward*, not to all `N` lines within one matrix application. This makes the containment weaker-consequence but not weaker-obligation. **One shared non-finite detector covers the whole wet path**, with ADR-003's existing semantics unchanged: latch, count, counter **not** cleared by `reset()`, no logging or allocation, and a deterministic full reset at the next block boundary that resets **every stage together** — a partial reset would leave a contaminated section feeding a clean network. Per testing.md a nonzero counter at the end of a run is a FAIL, never a pass.
8. **Lifecycle.** All diffusion delay lengths are derived at `prepare` from their time-domain specification at *this* `f_s`; a sample-rate change is a full re-`prepare` with full re-derivation, a full reset and a discarded tail, exactly as ADR-003 (d) specifies — no new rule. `reset()` zeros every diffusion delay line across its full reserved capacity and leaves `g_ap` and the lengths untouched, since they are configuration and not state. The zero state is an equilibrium for an allpass section (`v = 0 + g_ap·0 = 0`), so ADR-003 (d) rule 7's **silence-in/silence-out bit-exact canary extends over the whole wet path** and is the check that no diffusion state was missed. **`g_ap` is the one coefficient in the entire design that depends on neither `f_s` nor a delay length**, so it is structurally immune to ADR-002's "a cached coefficient silently changes the decay" hazard.
9. **Realtime safety (§19).** Per-sample work is `2(K_in + 2K_out)` multiplies, the same number of adds, `K_in + 2K_out` branchless cutoff selects, `N − 2` tap adds and the Mix gains — all fixed, none dependent on a sample value, a parameter value or a decay length. `prepare` remains the only allocating operation. Nothing here introduces a lock, an allocation, a data-dependent branch or a block-size dependence.

#### (i) Named verification cases (DS-1 … DS-12)

These are the numbers a future implementation must produce. **This ADR adds no validation gate to testing.md**; the gate is defined when the corresponding implementation milestone is authorized, the way ADR-003's and ADR-004's cases were consolidated into a verification-case specification before implementation.

| ID | Case | Bound / required record |
|---|---|---|
| DS-1 | Coefficient and pole bound | `g_ap` finite and in `[0, 0.9]`, enforced at `prepare`; a violation fails `prepare` and changes nothing. Record each section's pole radius `g_ap^{1/d}` and confirm `< 1` |
| DS-2 | Allpass magnitude, falsification | Dense grid of ≥ 2¹⁶ points on `[0, π]`: `\|A_j(e^{jω})\| = 1 ± 1e-6` per section, and for each full cascade. Required *even though* it is exact in theory, per ADR-002's "measured, never assumed" — the same reason NS-3 exists beside NS-2 |
| DS-3 | Delay-length derivation | Every diffusion delay prime, strictly increasing within its window, `< m_min`, and pairwise co-prime with every other diffusion delay and every `mᵢ` (checked by direct gcd, never inferred from primality), at both fixture rates. **The tie-break must be exercised**: it fires four times over the four configurations tabulated in (c) |
| DS-4 | Energy conservation and decay-law regression | Input and output energy of each cascade equal within a stated tolerance for impulse and broadband noise. Separately: the decay law measured through the **full** wet path still matches NS-6's, because no allpass stage and no tap can change `T60` — a regression check on the claim in (e) |
| DS-5 | Peak (ℓ1) headroom | Measured peak of the diffused signal for a full-scale impulse and for a deliberately worst-case aligned input, reported against the bounds `√5⁴ = 25.000` (input) and `√5² = 5.000` (per output chain). Overshoot **reported, never clipped** |
| DS-6 | Echo density | Measured arrival/echo density of the diffused path against the idealized lattice count in Evidence, at ≥ 3 times spanning 1–30 ms, and at the network's first arrival. **Recorded, not gated** — the model is amplitude-blind |
| DS-7 | Interchannel coherence | Magnitude-squared coherence and broadband correlation coefficient between `out_L` and `out_R`, for impulse and noise excitation, at ≥ 3 `T60₀` values and both fixture rates. Recorded with the explicit note that the zero-coherence prediction is **conditional on an unproven assumption** (Rationale) |
| DS-8 | Mono compatibility and the Mix consequence | `L + R` level and spectrum against each channel; the predicted `+3.01 dB` incoherent wet sum checked; the dry/wet mono bias measured across a full Mix sweep. Also the dry↔wet correlation ADR-004 assumed |
| DS-9 | Channel balance | `L` and `R` RMS equal within a stated tolerance for the same input; no systematic energy-centroid offset beyond the `2.63 ms` chain-length difference recorded in (f) |
| DS-10 | Numerical safety extension | The cutoff present at every diffusion recursive memory and nowhere else; non-finite substitution at the **head** of the input chain; shared detector counter surviving `reset()`; `reset(); reset()` indistinguishable from one; silence-in/silence-out **bit-exact** over the whole wet path; measured time-to-exact-silence against the additive bound in (h)(6) |
| DS-11 | Determinism and block partitions | Repeated renders byte-identical. Because the whole added path is time-invariant, outputs must be **identical** across partitions `{1, 13, 64, 512, ragged}` — a stricter requirement than PT-5's bounded difference, which exists only because automation is block-quantized |
| DS-12 | Cost and realtime safety | Measured per-sample cost with and without the diffusion stages; zero allocations during `process()`; per-sample instruction and branch counts independent of sample values; the bracket configurations `K_in ∈ {2,3,4,5}`, `K_out ∈ {1,2}` runnable |

#### (j) Explicit scope exclusions

This ADR decides input diffusion, injection, output taps, output diffusion/decorrelation and the stereo Mix convention. It does **not** decide or imply:

- **Any modulation of any kind.** ADR-002's prohibition and ADR-003 (e)'s Path A (A1–A4) and Path B (B1–B5) stand exactly as written. This ADR opens no third path and relaxes no bar. Modulated diffusion — Dattorro's modulated tank allpasses, §15's "feedback modulation" and "nonlinear diffusion", §16's "time-varying diffusion" — is **prohibited**, and a time-varying `g_ap` is prohibited with it. Fixing four more delay lengths as preparation-time constants makes the design more precisely specified, not more permissible to vary.
- **Any change to the late network.** Its topology, matrix family, per-line damping filter, decay law, denormal mechanism, detector semantics, lifecycle contract and every one of its eleven verified NS results are untouched. Nothing decided here may modify `FeedbackDelayNetwork`'s internals; the only change it permits is one additive accessor for the two group taps.
- **A Diffusion control or a Width control.** Both are charter §14 candidates and both are **DEFERRED to their own future ADR**, exactly as ADR-004 deferred Size, Mod Depth and Mod Rate. A Diffusion control would reach `g_ap` (and possibly `K_in`); a Width control would reach the tap weights or a mid/side interpolation. Neither mapping, range, clamp, smoothing contract nor transport is decided here, and (h)'s clamp on `g_ap` is a validation guard against a programming error, **not** a control range.
- **Pre-delay, input bandwidth conditioning, wet tone, or an early-reflection path around the network.** §33 and §11 name all of them; none is decided. Pre-delay remains deferred under ADR-004. The 27 ms gap before the first wet arrival is **not** closed by this ADR (Rationale).
- **`N_product`, the supported sample-rate matrix, `T60_min`, `T60_max`, `D_max`, `δ_margin`'s concrete value, delay-line interpolation, or the product's input channel configuration.** All remain exactly as ADR-002, ADR-003, ADR-004 and ADR-005 left them.
- **Freeze/Infinite (§17), Bloom (§16), Texture (§15), pitch or spectral processing (§18), multi-engine architecture (§39), UI, or the AUv3 framework (ADR-001).**
- **Any implementation.** No `.h`, `.cpp` or `CMakeLists.txt` file, no interface, no class structure, no file layout, no build target. That translation is Terra's, in a separate `docs/phases/` task plan, under separate authorization — the same ADR → plan → implementation cycle every prior decision in this file has followed.

### Rationale

#### Why an allpass cascade, and what "diffusion" actually buys that the network cannot

The late network's weakness is precisely located. ADR-005 computed it: the `N = 8` fixture meets the `T60`-scaled Schroeder–Logan mode-density criterion only up to `T60₀ ≈ 2.66 s`, with modal overlap falling to 0.03 at 30 s, and it named three levers — more lines, more total delay, or **echo** density from diffusion. The first two buy mode density at linear cost in memory and arithmetic and neither is available without reopening ADR-005. The third is different in kind: an allpass cascade raises echo density **without adding a single mode**, because it has no resonances to add — its magnitude response is exactly flat. That is the structural reason §33's V0 chain contains diffusion stages, and it is why this stage is the lever ADR-005 pointed at.

Smith's own framing is the clearest statement of the mechanism: a Schroeder allpass section expands "each nonzero input sample from the previous stage into an entire infinite allpass impulse response," which is why such sections "are sometimes called impulse expanders or impulse diffusers." Cascading `K` of them with mutually prime lengths puts arrivals at every non-negative integer combination `Σ k_j d_j`, so the arrival count up to time `n` grows as `n^K/(K!·Π d_j)` and the density grows as `n^{K−1}/((K−1)!·Π d_j)` — polynomial of order `K − 1`. That is the whole argument for a cascade over a single section, and it is what the density table in Evidence quantifies.

The alternatives fail on identifiable grounds rather than on taste. A lowpass adds no arrivals whatsoever and is a tone decision wearing a diffusion label. A recirculating allpass tank raises exactly the objection ADR-002 recorded when it rejected that topology as the late network — its bound is on a product of loop gains rather than a single coefficient inequality — and Aetherfield does not need a second recirculating structure in front of the one it has proven. Nested allpasses are genuinely better per unit of memory and keep the allpass property, and they are deferred rather than rejected for the reason ADR-003 gave when it deferred the higher-order attenuation filter: the escape hatch is pre-analyzed, it is simply not taken, because the nesting depth and inner/outer split are free choices for which nothing in this repository supplies a criterion.

#### Why `K_in = 4`, argued from two quantities rather than from a convention

Two derived quantities disagree about how many stages to use, and the decision is made by reading both rather than by citing a published stage count.

The first is **echo density**, and it is a weak discriminator. Computing the crossing point of Schroeder's ≈1000-echoes-per-second figure from the decided rule gives 9.79 ms at `K = 2`, 7.85 ms at `K = 3`, 8.38 ms at `K = 4` and 9.21 ms at `K = 5` — essentially flat, because adding a stage also adds total delay. The density *at* the network's first arrival, where it matters, does discriminate: 2 765 / 11 876 / 33 755 / 74 567 arrivals per second for `K = 2/3/4/5`. But all four are above the 1000/s figure by a wide margin, and the count is amplitude-blind, so a stopping rule built on it alone would be built on the weaker of the two numbers.

The second is **how much input energy reaches the network without having been scattered at all**, and it is exact. A cascade's direct (zero-delay) term is `(−g_ap)^K`, so the unscattered energy fraction is `g_ap^{2K}`, which at `g_ap = 0.618` falls by exactly `20·log₁₀ g_ap = −4.180 dB` per stage: `−8.36 dB` at `K = 2`, `−12.54 dB` at `K = 3`, `−16.72 dB` at `K = 4`, `−20.90 dB` at `K = 5`. This is a quantity with a clear meaning — the component of the input that arrives at the network exactly as it left the host — and it is not amplitude-blind.

`K_in = 4` is the smallest stage count placing the unscattered component below `−15 dB`. **The `−15 dB` threshold is a judgment and is labelled as one**; the quantity it is applied to, and the `4.18 dB`-per-stage law, are arithmetic. `K = 3` would leave the unscattered component at `−12.5 dB`, comparable in level to the first few line arrivals; `K = 5` buys another 4.18 dB for 25 % more cost and memory in a region the density figures already describe as saturated. That the answer coincides with Dattorro's four input diffusers is a consistency check, recorded as one, and not the reason.

#### Why `g_ap` is derived rather than cited

The literature's values for input diffusion coefficients — Schroeder's typical `0.7`, Dattorro's `0.625` and `0.750` — are a range, not a derivation, and adopting one would be the "asserted rather than derived" move every ADR in this file has avoided. The criterion used instead is structural and has a one-line consequence: setting the direct term equal to the first echo, `g_ap = 1 − g_ap²`, is the unique choice at which the section's own impulse response does not begin with a step between its first two arrivals. It is worth noting what the algebra then hands back for free: `1 − g_ap² = g_ap` exactly, so the section's impulse response is `{−g_ap, g_ap, g_ap², g_ap³, …}` — a clean geometric train after the first term — and `‖h‖₁ = 1 + 2g_ap = √5` exactly, which is what makes the headroom bound in (h)(3) a closed form rather than a numerical evaluation.

The honest fence: **this criterion is not a perceptual claim.** It does not say `0.618` sounds better than `0.7`. It says the value follows from a stated property rather than from a table, so a later measurement that prefers a different value will be changing a *derivation*, which is a visible act, rather than re-tuning a number nobody can trace.

One consequence must be recorded because it will be heard before it is measured: a diffusion section is **not** instantaneous. Its internal echo train decays 60 dB in `−3/log₁₀ g_ap = 14.35` circulations, which for the longest input section (`479` samples at 48 kHz) is `143 ms`. The input diffuser therefore spreads wet-path energy over roughly that timescale — which is the diffusion it exists to perform, and also the reason an allpass chain is not transparent. The **dry path is untouched**, so transients survive in the dry signal; only the wet path is smeared.

#### Why uniform injection, and why the argument against it does not hold

There is a tempting objection to uniform injection: with a Sylvester–Hadamard matrix, the all-ones direction is structurally special, and `A·1 = √N·e₀`. **That objection is wrong for this network, and it is recorded here so it is not raised again as though it were right.** The matrix never sees the all-ones vector, because the `N` lines have distinct, mutually prime lengths: the input injected at sample `n` re-emerges from line `i` at `n + mᵢ`, and those are `N` different sample instants. No vector-level degeneracy survives the delay bank. What remains true is only that every line receives the same signal at the same instant, and since they re-emit at different times, the lines are decorrelated by their lengths alone.

Given that, the choice among (b1), (b2) and (b3) reduces to which free choices each one creates. (b1) creates none, satisfies both stated criteria — unit ℓ2 norm and equal per-line energy — costs one multiply, and requires no change to a class whose eleven verification cases are already passing. (b2) is identical on every criterion and adds a seven-way arbitrary choice. (b3) has `K_in^N` arbitrary assignments and would deliberately feed some lines a less scattered signal. §43's preference for the smaller and more understandable option decides it, and the two rejected options are recorded as pre-analyzed successors precisely so that a later measurement can take one without re-deriving this reasoning.

The one substantive consequence is not about the vector at all: it is that **input conditioning has to move upstream**. ADR-003 placed non-finite substitution at the network's input because that was the front of the signal path. It no longer is, and a NaN entering a diffusion section would be trapped in that section's own loop and would then feed the network forever. Noticing that the guard's *position* is a consequence of the chain's *shape* is the kind of thing that is cheap now and expensive after implementation.

#### Why disjoint interleaved taps, and exactly how much the decorrelation claim is worth

The design target is low interchannel coherence, and the reason is literature rather than preference: apparent source width and spaciousness are associated with **low correlation between the signals reaching the two ears**, a relationship established by Barron and Marshall's work on lateral reflections and developed by Griesinger for the source-width and envelopment case. Kendall's treatment of audio decorrelation is the direct methodological ancestor of using allpass filters — which change phase while leaving magnitude alone — as the decorrelation mechanism. This ADR adopts that target; it does not claim to hit it.

What *is* provable, and exactly under what assumption, must be stated precisely. **Assume the `N` internal line signals are mutually uncorrelated with equal power.** Then `E[y_L·y_R] = σ²·⟨c_L, c_R⟩ = 0` for any orthogonal tap pair, and if the assumption is extended to all lags, the interchannel coherence is zero at every frequency. Disjoint supports make `⟨c_L, c_R⟩ = 0` in the strongest available way: the two channels are built from **different delay lines**, not merely from different weightings of the same ones.

**The assumption is not true in detail, and nothing here proves it.** The feedback matrix broadcasts each line's output into every line, so `vᵢ` and `vⱼ` do share history, at lags set by sums and differences of the delays. What mutual primality buys is that those shared-history lags are scattered rather than coincident — which is the same argument ADR-005 made about mode coincidence, with the same fence it attached: it removes a known way to get correlation, it does not prove the realized correlation is zero. The early part of the response is where the assumption is weakest, since the first arrivals are a handful of discrete events. **The realized coherence is therefore a measured quantity (DS-7), and the prediction above is a conditional statement, not a result.** This is the same discipline ADR-003 applied when it proved the finite-time silence bound and left the internal peak bound to NS-7: prove what is provable, measure what is not, and never let the second masquerade as the first.

The interleave criterion is the one place where doing the arithmetic changes an otherwise-arbitrary choice. Both the even/odd split and a first-half/second-half split give `⟨c_L, c_R⟩ = 0` and equal channel norms. But the second would give the left channel the four shortest lines (27–43 ms at 48 kHz) and the right channel the four longest (50–81 ms), which is a systematic time asymmetry between channels in the exact region where the response is sparsest and most audible as discrete events. Even/odd gives each channel `{27, 37, 50, 69} ms` and `{31, 43, 59, 81} ms` — both spanning the full range. That criterion is stated, checkable, and decides the assignment; the sign inside each group is not decided by anything, and is labelled as such rather than dressed up.

The cost of disjoint taps is equally concrete and is the reason the alternative was seriously considered: the mono sum `c_L + c_R` uses all `N` lines coherently, but each channel's **direct** tap uses only `N/2`. Two orthogonal `±1` rows would give the opposite trade — every line in every channel, but half the lines cancelling in the mono sum. Disjoint taps were selected because the direct-tap deficit is confined to the onset, which is exactly the region the input and output diffusion stages exist to fill, whereas a mono sum that silently discards half the network is a defect with no other stage to repair it.

#### Why output diffusion is a separate stage rather than more input diffusion

Input diffusion and output diffusion do different jobs, and the difference is that one is before the feedback and one is after it. Density added at the input is recirculated by the network and compounds with it; density added at the output is applied once, to the tapped signal, and is the only mechanism available for making the two channels differ *after* the tap. Merging §11's `POST` and `WIDTH` stages is what makes this one stage instead of two: an allpass pair with different lengths per channel is simultaneously a diffuser and a decorrelator, and — because `|A| = 1` at every frequency — it can widen without altering either channel's magnitude spectrum. A width mechanism that changes the tone is a tone control; this one is not.

`K_out = 2` rather than 1 gives quadratic rather than linear density growth per channel at a cost of four multiplies and four adds per sample, and rather than 3+ because the input chain already supplies the bulk of the density and §43 rules against paying for more where the numbers say the region is saturated. Both `K_out = 1` and `K_out = 2` are required runnable bracket configurations, so this is a measurable choice rather than a settled one.

The mono-compatibility question deserves the sharp version rather than the reassuring one. Applying different allpass filters to two channels of a **correlated** pair is the classic way to destroy a mono sum, because the two copies then comb-filter against each other. That failure mode is absent here **only because the inputs to the two chains are the orthogonal taps** — under the same uncorrelatedness assumption as before, the cross term vanishes and mono power is exactly preserved, since `|A_L| = |A_R| = 1`. The conditional structure is identical to the coherence claim: the mechanism is sound, the premise is an assumption, and DS-8 is where it becomes evidence.

#### Why the dry signal is centred at full gain, and what that costs

ADR-004's equal-power law was derived for a mono dry against a mono wet. Extending it to a stereo wet forces a choice that has no cost-free answer, because dry and wet sum differently: the dry component is identical in both channels and sums coherently at `+6.02 dB`, while the wet channels are (by design) uncorrelated and sum at `+3.01 dB`. Any convention that makes per-channel power constant across the Mix sweep therefore makes the mono sum dry-biased by exactly `3.01 dB`, and any convention that fixes the mono sum makes per-channel power vary. Centring the dry at full `dry(m)` selects per-channel constancy, on the ground that stereo is the primary listening case and that it leaves ADR-004's decided law literally unmodified rather than reinterpreted. The `1/√2` alternative is named, the arithmetic is stated, and the consequence is measured at DS-8 rather than left for someone to discover in a mono fold-down.

#### What this ADR is predicted *not* to fix

Stated in advance, as ADR-005 did, so that a disappointing audition is not misread as evidence against these decisions.

- **The 27 ms gap before the first wet arrival is unchanged.** The input diffuser sits *before* the network, so nothing it produces reaches the output except through a delay line, and ADR-005 fixed `t_min = 27 ms`. The wet path's first non-zero sample is still at `m_min`, and ADR-005's prediction — "a sparse, audibly discrete onset is a structural certainty of this fixture" — is only partly addressed: the arrivals after 27 ms are now dense, but the interval before them is still silent. The levers are a shorter `t_min` (reopens ADR-005), a decided pre-delay with an early-reflection path around the network (not decided here, and a real architectural addition), or nothing at all — an initial gap is what a large space does. This ADR takes none of them and names them so the choice stays visible.
- **Mode density is unchanged.** Echo density and mode density are different quantities, and an allpass adds no modes. ADR-005's finding that the fixture meets the `T60`-scaled Schroeder–Logan criterion only up to `T60₀ ≈ 2.66 s` stands exactly as recorded. If long-decay ringing is heard, this ADR is not the fix, and `N` or `Σtᵢ` is.
- **No claim is made that the stereo image is good.** Orthogonal taps and unit-magnitude decorrelators are structural facts. Whether the resulting width is convincing, whether it collapses on mono fold-down, and whether `g_ap = 0.618` with four stages sounds smooth rather than smeared are perceptual questions answerable only by testing.md's **Sonic acceptance** gate. DS-6 through DS-9 are inputs to that judgment, not substitutes for it.

### Evidence

**No Aetherfield measurement supports this decision. The repository has measured nothing about diffusion or stereo (testing.md), and that remains true after this ADR, because neither exists.** The computations below were performed while drafting it, from the decided rules and constants, and are reproducible from those rules alone; they are arithmetic, not observations of Aetherfield code.

- **The allpass identities at `g_ap = (√5 − 1)/2`.** `g_ap² + g_ap = 1.000000000000`; `1 − g_ap² = g_ap = 0.618033988750` (so the equal-first-two-arrivals criterion is satisfied by construction); total impulse-response energy `g_ap² + (1 − g_ap²) = 1.000000000000` exactly, confirming the section is energy-preserving; `‖h‖₁ = 1 + 2g_ap = 2.236067977500 = √5` exactly. Unscattered direct-term energy `g_ap^{2K}` = `−4.180 / −8.360 / −12.539 / −16.719 / −20.899 dB` for `K = 1…5`, i.e. exactly `20·log₁₀ g_ap = −4.180 dB` per stage. Internal 60 dB decay `−3/log₁₀ g_ap = 14.355` circulations, which is `14.0 ms` for the shortest input section and `143 ms` for the longest, at 48 kHz.
- **The delay sets, their primality, co-primality and ordering.** For both fixture rates and both windows, the rule in (c) produced strictly increasing sets of distinct primes; at each fixture rate, across the four input lengths, the four output lengths and ADR-005's eight late-network lengths — sixteen values — **every pair is co-prime** (`gcd = 1`, checked directly, not inferred from primality) and every value is distinct. The largest displacement of a realized time from its target was `2.78 %` (44.1 kHz, input stage 3), against an intended adjacent-stage ratio of `2.154` in the input window — the intended spread survives the prime snap by a factor of roughly 40. **ADR-005's tie-break fired four times** (input @ 44.1 kHz stages 3 and 4; output stage 1 at both rates); the totality guard never fired.
- **Idealized echo density of the input chain.** Using the arrival-lattice model `density(n) = f_s·n^{K−1}/((K−1)!·Π d_j)`, evaluated over the decided rule at 48 kHz: the chain crosses Schroeder's ≈1000-echoes-per-second figure at `n` = 470 / 377 / **402** / 442 samples (9.79 / 7.85 / **8.38** / 9.21 ms) for `K` = 2 / 3 / **4** / 5, and reaches 2 765 / 11 876 / **33 755** / 74 567 arrivals per second at `n = 1297` (27 ms, the late network's first arrival). **These are amplitude-blind lattice-point counts**, exactly as ADR-005 labelled its own path counts — a typical lattice point at 27 ms has passed through roughly six section circulations and so carries an amplitude of order `g_ap⁶ ≈ −25 dB`. They are also not comparable to published per-second figures for other allpass cascades, because the density of a cascade grows with time and depends entirely on its delay set. For contrast, ADR-005 computed the late network **alone** crossing the same figure only between 100 and 150 ms.
- **Stability, headroom and silence bounds over the decided constants.** Section pole radius `g_ap^{1/d}` = `0.98980` at `d = 47` and `0.99900` at `d = 479` — strictly inside the unit circle at every length. Worst-case peak (ℓ1) gain `√5^{K}` = `25.000` for the input chain and `5.000` for each output chain. Time to exact silence with `ε = 1e-20` from unit state: `ln(ε)/ln(g_ap) = 95.70` circulations, giving `≤ 1.699 s` (input chain), `≤ 0.993 s` (L) and `≤ 1.244 s` (R) at 48 kHz. ADR-003's `ε` revisit trigger evaluates to `5 × 10⁻²⁰ = −386.0 dBFS` at the output and does not bind. With `‖b‖₂ = 1`, ADR-003's internal-gain bound `G = 1/(1 − γ₀^{m_min})` over ADR-005's margins is `21.9 / 54.1 / 161.3 / 321.5` (`+26.8 / +34.7 / +44.2 / +50.1 dB`) at `T60₀ = 4 / 10 / 30 / 60 s` — unchanged by the diffusion stages, because the input chain preserves the injected signal's norm exactly.
- **Tap norms and the mono sum.** At `N = 8` with the `1/√(N/2) = 0.5` normalization, `‖c_L‖₂ = ‖c_R‖₂ = 1.000000` and `⟨c_L, c_R⟩ = 0` by disjoint support; `‖c_L + c_R‖₂ = √2`, so under the uncorrelatedness assumption the mono sum's wet power is `2.000000×` a single channel's, i.e. `+3.010 dB` — the incoherent-sum result, and the source of the `3.01 dB` dry bias recorded in (g).
- **Cost and memory (counts, not cycles, and not a CPU budget — ADR-005's caveat, unchanged).** Per sample: 20 operations for the input chain, 20 for the two output chains, 8 for the taps, 6 for Mix and 1 for injection scaling — **55**, against ADR-005's 72 core-loop operations at `N = 8`, for a total of **127 operations per sample, 6.10 Mops/s at 48 kHz**. Diffusion delay memory is `852 + 1122 = 1974` floats = **7.71 KiB**, against the late network's 123.5 KiB at `N = 8` — a 6 % increase.
- [Schroeder, "Natural Sounding Artificial Reverberation," *JAES* 10(3), July 1962, pp. 219–223](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Schroeder_1962.pdf) — the series-allpass ("colorless") construction this ADR adopts for the diffusion stages. Already load-bearing in ADR-002, where the same structure was rejected *as the late network*.
- Smith, *Physical Audio Signal Processing* (CCRMA): [Allpass Filters](https://ccrma.stanford.edu/~jos/pasp/Allpass_Filters.html) — "the amplitude response of an allpass filter is 1 at each frequency, while the phase response … can be arbitrary," with "allpass" used in the strict unity-gain sense. This is the property (h)(2) rests on. [Schroeder Allpass Sections](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html) — the cascaded structure, "A typical value for g is 0.7," that "the delay-line lengths `Mᵢ` are typically mutually prime and spanning successive orders of magnitude, e.g., 1051,337,113," and the mechanism this ADR relies on: each section "expand[s] each nonzero input sample from the previous stage into an entire infinite allpass impulse response," which is why they are "sometimes called impulse expanders or impulse diffusers." The published `g = 0.7` is cited as the range check in (b), not as the derivation. [Schroeder Reverberators](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberators.html) — allpass impulse responses "are only colorless when they are extremely short (less than 10 ms or so)" and "Longer allpass impulse responses sound similar to feedback comb-filters." This is the source of `d_max = 10 ms` and it is the same citation ADR-002 used to reject the allpass tank as the late network; the bound is now satisfied by construction rather than argued around. [Choice of Delay Lengths](https://ccrma.stanford.edu/~jos/pasp/Choice_Delay_Lengths.html) — mutual primality and its purpose, already load-bearing in ADR-005 and reused unchanged for the diffusion lengths.
- [Dattorro, "Effect Design, Part 1: Reverberator and Other Filters," *JAES* 45(9), September 1997](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf) — the plate design's **four series input diffusers** ahead of the tank, its input bandwidth filter, its diffusion coefficients (`0.750` and `0.625`), and the figure-eight tap structure evaluated as alternative (c3). Cited for the input-diffuser stage count and coefficient range as a consistency check, and for the tap alternative. **Its delay values are tabulated sample counts at 29.76 kHz, which is exactly the form ADR-002 excluded**, and none of them is adopted. [Part 2: Delay-Line Modulation and Chorus, *JAES* 45(10), October 1997, pp. 764–788](https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf) — cited **only** to fence it off: the plate modulates its tank allpass delays, and no part of that is adopted or authorized here.
- Gardner, "Reverberation Algorithms," in Kahrs and Brandenburg (eds.), *Applications of Digital Signal Processing to Audio and Acoustics*, Kluwer, 1998, pp. 85–131 — nested allpass structures, alternative (a2), deferred as the named upgrade path. Cited bibliographically; no URL is asserted.
- Kendall, "The Decorrelation of Audio Signals and Its Impact on Spatial Imagery," *Computer Music Journal* 19(4), Winter 1995, pp. 71–87 — allpass/phase-randomising filters as the mechanism for decorrelating audio channels without altering their magnitude spectra. This is the methodological basis for (f). Cited bibliographically.
- Barron and Marshall, "Spatial impression due to early lateral reflections in concert halls: the derivation of a physical measure," *Journal of Sound and Vibration* 77(2), 1981, pp. 211–232; and Griesinger, "The Psychoacoustics of Apparent Source Width, Spaciousness and Envelopment in Performance Spaces," *Acta Acustica* 83, 1997, pp. 721–731 — the association between low correlation of the signals at the two ears and perceived width, spaciousness and envelopment. Cited to establish **why low interchannel coherence is the design target**; neither is evidence that this design achieves it. Cited bibliographically.
- [Jot and Chaigne, "Digital Delay Networks for Designing Artificial Reverberators," AES 90th Convention, February 1991](https://aes2.org/publications/elibrary-page/?id=5663) — the homogeneous decay law whose consequence, in (e), is that no tap design can change the realized `T60`. Already load-bearing in ADR-002, ADR-003 and ADR-005.
- [Reyes, "Multichannel Intensity Panning" (CCRMA, Stanford)](https://ccrma.stanford.edu/guides/planetccrma/Multichannel_Intensity_Pann.html) — the constant-power law underlying ADR-004's Mix, cited again because (g) extends its application without changing it.

### Consequences

Aetherfield now has a complete V0 wet signal path on paper — diffuse, inject, circulate, tap, decorrelate, mix — and every stage added by this ADR is either outside every feedback loop or inside a loop whose stability rests on a single inequality, `|g_ap| < 1`, that a reviewer can check by inspection and that does not depend on any delay length. The already-proven late network is untouched: its topology, matrix, damping, decay law, denormal mechanism, detector and lifecycle are unchanged, its eleven NS results stand, and the only thing this ADR asks of it is one additive accessor that computes nothing new. The test-only injection/tap convention that testing.md and docs/phases/phase1-s2-plan.md have both been careful to label as never a product decision now has a decided replacement, and that label can be retired when the replacement is implemented.

Two prior open items are partly closed. ADR-005's density shortfall now has its named lever decided rather than merely named: the input chain reaches Schroeder's echo-density figure roughly an order of magnitude earlier in time than the late network does alone, without adding a single mode. ADR-004's admitted weakness in the equal-power Mix law — that its decorrelation premise is weakest for the earliest wet energy — is strengthened rather than proven, and is now a measured quantity with a case attached.

The decision creates obligations, and they are specific. **Input conditioning moves upstream**: non-finite substitution must happen at the head of the diffusion chain, because the network's own guard is no longer the front of the signal path, and a NaN admitted into a diffusion section is trapped in that section's loop and then feeds the network permanently. **The non-finite detector becomes shared across the whole wet path**, and its deterministic reset must clear every stage together; a partial reset would leave a contaminated section feeding a clean network. **Eight new recursive memories — one per diffusion section — join the network's `2N`** as places where ADR-003's cutoff must appear, and where `reset()` must zero state that no block diagram drawn at topology level shows — the same class of omission ADR-003 warned about for the damping filter state, and the silence-in/silence-out canary is again the check that catches it. **The diffusion stages are not peak-non-expansive**: `√5` per section, `25.0` for the input chain, so headroom is now a measured quantity with a stated bound rather than an unexamined assumption. **ADR-005's tie-break stops being inert** and must be exercised by a test. And **the mono sum is dry-biased by exactly 3.01 dB** across the Mix sweep, which is a stated design consequence rather than a defect, and which any user-facing material must not contradict.

Several things stay deliberately open and must not drift into being treated as settled. A **Diffusion** control and a **Width** control are named, their internal targets identified, and both **DEFERRED to their own ADR** — `g_ap`'s validation guard is not a control range, and a future Diffusion control would need its own smoothing argument, for which one favourable fact is recorded in advance: unlike the damping coefficient, a time-varying allpass coefficient bounded by `ḡ < 1` admits a direct small-gain state bound `1/(1 − ḡ)` (`2.618` at `g_ap`), because the element it multiplies is a pure delay rather than a filter with memory. Its output bound is `(1 + ḡ)/(1 − ḡ) = 4.236`. **That is not authorization, and it does not make the cascade allpass under time variation** — `|A| = 1` is an LTI statement, and every energy-preservation and mono-sum argument in this ADR is void for a time-varying `g_ap`. Pre-delay, input bandwidth conditioning, wet tone, an early-reflection path, `N_product`, the supported sample-rate matrix, `T60_min`/`T60_max`/`D_max`, interpolation and the product's input channel configuration all remain exactly as the prior ADRs left them.

**Modulation is not moved one step closer to authorization by this ADR.** ADR-002's prohibition and ADR-003 (e)'s Path A and Path B bars stand unmet; the cited designs' modulated diffusers are explicitly not adopted; and fixing eight more delay lengths as preparation-time constants makes the design more precisely specified, not more permissible to vary.

### Revisit When

DS-6 or the Sonic acceptance gate shows the series cascade's echo density insufficient, at which point the named successors are nested allpass sections (a2), a larger `K_in` within the decided bracket, or Dattorro-style intra-line taps (c3) — and the choice must be argued from the measurement rather than from the lattice count, which is amplitude-blind; DS-7 shows interchannel coherence materially above zero, which would falsify the uncorrelated-lines assumption (c1) rests on and make a signed or figure-eight tap design the successor; DS-8 shows the mono fold-down unacceptable, at which point the `1/√2`-centred dry alternative in (g), or a signed tap pair, becomes the successor decision and the 3.01 dB arithmetic is the reason; DS-5 records a diffusion peak near the `√5^{K}` bound under ordinary programme material rather than under a constructed worst case, which would make the ℓ1 bound a design constraint rather than a note; a **Diffusion** or **Width** control is accepted, which requires its own ADR deciding range, clamp, mapping and a smoothing contract that must re-argue ADR-004 (d) for a coefficient the present ADR deliberately holds constant; a pre-delay or early-reflection path is accepted, which is the only thing that closes the 27 ms onset gap this ADR explicitly does not close; ADR-005's `t_min`, `N` or delay set changes, which invalidates the `d_max < t_min` guarantee that makes cross-set distinctness automatic and re-derives the density figures; a sample rate is supported at which ADR-005's totality guard fires inside a diffusion window, at which point the guard's behaviour stops being inert there as the tie-break already has; the product's input channel configuration is decided as stereo, which requires a second input diffusion chain with its own delay set and reopens (d) and (g); or modulation is proposed, at which point ADR-002's prohibition and ADR-003 (e) govern, every allpass identity in this ADR becomes void for the modulated element, and the fixed unmodulated baseline must be retained.
