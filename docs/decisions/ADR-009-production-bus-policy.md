---
id: "ADR-009"
status: accepted
accepted: "2026-09-18"
implementation: "none; architectural decision only, no wrapper/UI/dependency code"
review: "owner-accepted-2026-09-18"
review_document: null
depends_on: [ADR-001, ADR-003, ADR-004, ADR-006, ADR-007]
---

# ADR-009 — Production audio bus layout, dry/bypass, and buffer-aliasing policy

## Review summary

- **Decision:** the production bus is **stereo-in/stereo-out (alternative
  (B))**, with a sum-to-mono reduction feeding the existing unmodified mono
  diffusion chain. A dry/bypass and wet-tail policy at decision level, and
  a buffer-aliasing policy, are also decided — without authorizing any
  wrapper, UI, or dependency code.
- **Why:** ADR-007 named production bus layout, dry/bypass behavior, and
  buffer-aliasing policy as open prerequisites to any wrapper implementation
  plan, and stated plainly that "DS-B's mono-to-stereo route does not decide
  these." The owner chose (B) over the as-is mono-in/stereo-out route (A) to
  match conventional AUv3 host insert-slot expectations, accepting the cost
  of an extra reduction step and the loss of input stereo *image* (not just
  level) until/unless a true stereo-diffusion successor to ADR-006 (g) is
  built.
- **Consequence:** the sum-to-mono reduction is a new wrapper-side step
  ahead of the unmodified `src/dsp/` chain; it does not touch `src/dsp/`
  internals. Buffer-aliasing policy and the zero/variable-block invariant
  are decided here as grounded technical findings from the current source.
  Several other bypass/tail behaviors remain owner-gated (see "Remaining
  decisions and later evidence").
- **Uncertainty:** whether sum-to-mono reduction is a permanent product
  behavior or only an interim step pending a true stereo-diffusion
  successor; the bypass CPU-vs-tail-continuity tradeoff; whether a
  host-exposed "kill tail on bypass" control should exist; and whether
  multi-channel/surround output is ever a target — all remain unresolved
  and require explicit owner sign-off (see "Remaining decisions").

**Status: Accepted (2026-09-18) — architectural decision only, no
implementation.** The owner selected alternative (B), stereo-in/stereo-out
with sum-to-mono reduction, resolving this ADR's single biggest open
question. This record still authorizes no wrapper, UI, dependency, or
other implementation. A separately authorized bounded implementation plan
is still required before any code is written against it, exactly as
ADR-007's acceptance did not authorize a wrapper plan on its own.

**Status update (2026-09-19).** Three more of this ADR's originally-listed
uncertainties are now decided, and one technical recommendation is
recorded: sum-to-mono is an explicit interim step, not permanent (§1);
the bypass CPU-vs-tail-continuity tradeoff is decided as a hybrid
approach (§2); multi-channel/surround output is decided as not a target
("Remaining decisions"); and `canProcessInPlace = true` is recorded as a
source-grounded recommendation pending implementation-time confirmation
(§3). The host-exposed "kill tail instantly on bypass" affordance remains
explicitly deferred until UI is scoped — the owner declined to decide it
now rather than leaving it accidentally open. See §1/§2/§3 and "Remaining
decisions and later evidence" below for the dated detail.

## Context and scope

[ADR-007](ADR-007-auv3-integration-comparison.md), accepted 2026-09-18, named
this ADR's subject matter as an explicit open prerequisite in its
"Alternatives and ownership" table:

> Decide production bus layouts and buffer aliasing policy before wrapping;
> retain bounded render work. DS-B's mono-to-stereo route does not decide
> these.

and in "Remaining decisions and later evidence":

> production input/output layouts and dry/bypass behavior... Do not infer
> stereo-input support from the current evaluation route or product preset
> compatibility from normalized values alone.

[ADR-006](ADR-006-diffusion-stereo.md) accepted, measured, and closed the
DS-B **evaluation baseline** — an input-diffusion → normalized FDN injection
→ pre-step even/odd taps → output-diffusion → Mix route that takes one mono
input and produces two output channels
(`src/dsp/DiffusionStereoPath.h`/`.cpp`). ADR-006's own correction-note
contract (2026-09-17) and original record are explicit that this is a fixed
**evaluation baseline, not a proved stereo or perceptual result**, and that
it authorizes "no product signal path, host integration, UI." ADR-006 (g)
separately states: "Channel configuration of the input is DEFERRED... The
structure extends by adding a second input diffusion chain with its own
delay set; that is architectural information, not authorization." This ADR
does not reopen ADR-006's topology, tap design, or measured findings, and it
does not promote DS-B's specific tap/diffusion parameters into a product
configuration.

[ADR-001](ADR-001-portable-core.md) fixes the boundary this ADR must respect:
no Apple or JUCE types belong in `src/dsp/`; any bus/bypass/aliasing policy
decided here is a **wrapper-boundary** decision, not a change to the portable
core's interfaces.

[ADR-003](ADR-003-numerical-safety.md) fixes the numerical-safety contract
this ADR draws its bounded-tail-time argument from: the deterministic
denormal cutoff (`ε = 1e-20`), the proof that the cutoff drives every
recursive state to *exactly* zero within a computable number of circulations,
and the finite-time-silence bound `T_silence`. ADR-003's "Revisit When" names
one condition this ADR's tail-time claim depends on staying unviolated: `ε`
must not be lifted by "a large output gain... placed after the network" — a
concern ADR-006 (h)(6) already closed by arithmetic for the DS-B evaluation
route's specific output gain, and which any production output stage
(including a bypass/dry-mix stage) must re-close for its own gain structure
rather than inherit unchecked.

This ADR decides no wrapper interface, no class, no file, no build target,
and no parameter-event bridge ([ADR-008](ADR-008-parameter-event-bridge.md)
remains a separate open prerequisite, unaffected by this record). It decides
no change to `FeedbackDelayNetwork`, `DiffusionStereoPath`, or any other
`src/dsp/` component's internals.

## Evidence — what the current source actually does

Read directly from `src/dsp/DiffusionStereoPath.h`/`.cpp` and
`src/dsp/FeedbackDelayNetwork.h`, and from `docs/testing.md`'s DS-B measured
evidence (not inferred):

- **Bus shape today.** `DiffusionStereoPath::process(const float* mono,
  float* left, float* right, std::size_t count)` takes one 1-channel input
  array and two independent 1-channel output arrays. There is no interleaved
  or bus-list abstraction; "mono-in, stereo-out" is the literal shape of the
  function signature, not a description imposed from outside.
- **Zero-length blocks are already safe.** Both `DiffusionStereoPath::process`
  (`if (count == 0 || !state_) return;`) and `FeedbackDelayNetwork::process`
  (`if (count == 0) { return; }`) special-case `count == 0` as a no-op.
  `FeedbackDelayNetwork.h`'s own contract comment states "count == 0 is
  always a safe no-op." Per `docs/phases/phase1-ds-integration-plan.md`
  Task 3, this is a decided, tested contract: "Zero-frame call neither
  processes audio nor consumes a pending reset."
- **Variable-length blocks are already exercised.** `docs/testing.md`'s DS-11
  records repeated renders bit-identical and a ragged `{7,29,3,211,5}`
  partition bit-identical to a whole-buffer render, extending existing
  `{1,13,64,512,3}` fixed-partition coverage to DS-11's full required
  `{1,13,64,512,ragged}` set. Nothing in the per-sample loop or in
  `FeedbackDelayNetwork::prepare()`'s actual parameter list (sample rate,
  line count, min/max delay time, T60 endpoints — no block-size argument)
  imposes a fixed or maximum block-size precondition.
- **Aliasing today is foreclosed by channel-count mismatch, not decided by
  the algorithm.** `DiffusionStereoPath::processOne` reads its `mono` sample
  argument by value (it is passed into `processOne(mono)`, not read from the
  array again inside the per-sample body) before either output channel is
  written for that index, and it never reads a different index of `mono[]`.
  That means, narrowly, one output channel *could* safely alias the input
  array under the current sequential implementation — but the input is a
  single mono channel and the output is two channels, so at most one of
  `left`/`right` could ever occupy that memory; the other output channel
  categorically cannot, because there is only one input array and it cannot
  simultaneously be two distinct output buffers. **The channel-count
  mismatch, not any property of the DSP algorithm, is what forces separate
  input and output storage.** This is a structural fact of the current
  bus shape, independent of whatever future bus layout is chosen.
- **The existing tap design has a measured, non-negligible L/R asymmetry.**
  `docs/testing.md` DS-9 records L arriving at exactly `m_0` and R at exactly
  `m_1`, "R arrives ~4.5ms later than L," attributed to the even/odd tap
  interleave assigning the network's shortest (higher-energy) lines to the L
  channel; and an L/R RMS imbalance of 0.6300719dB @48kHz / 0.5860281dB
  @44.1kHz, "within the declared gate but only ~37% margin." `docs/testing.md`
  records this as "a recorded finding, not a bug," and explicitly
  uninterpreted pending review. Any claim that the DS-B route's output taps
  already constitute a validated production stereo image is unsupported by
  this evidence.
- **Bounded, not infinite, tail is provable.** ADR-003 (b)(6)/(h) prove that
  the deterministic cutoff drives every recursive memory to exactly zero
  within a computable number of circulations once state magnitude falls
  below `ε = 1e-20`, giving `T_silence` as a closed-form bound in terms of
  the configured decay/damping coefficients. `docs/testing.md` DS-10 records
  a concrete measured full-path silence figure for one declared fixture
  (321,914 samples @48kHz / 297,234 @44.1kHz), explicitly "not compared to
  the proof-template `D_j` or any historical additive bound" (correction
  note C3) — i.e., this is a real, finite, decay-setting-dependent quantity,
  not an assumption of "the tail rings forever."
- **ADR-007's cited host contract.** `AUInternalRenderBlock` and
  `AUParameterEvent` (Apple documentation, cited in ADR-007's evidence table)
  establish that render events carry timing/ramp information and that
  "buffer lifetime and input-pull responsibilities are explicit" — i.e.,
  variable- and zero-length render callbacks are a documented host
  capability the wrapper must tolerate, not an edge case.

## Alternatives: production input/output bus layout

| Criterion | (A) Mono-in / stereo-out effect bus (as DS-B route stands) | (B) Stereo-in / stereo-out effect bus, sum-to-mono reduction into the existing chain | (C) No-input generator/instrument bus |
|---|---|---|---|
| Matches AUv3 host convention for an *effect*-type AU | Atypical: most iOS/iPadOS host effect chips assume matching input/output channel count per bus; a mono-in/stereo-out effect is unusual and some hosts' insert UI may not offer it cleanly | Matches convention: symmetric stereo bus is the default effect shape hosts expect on an insert | Not an effect at all — hosts categorize this as a generator/instrument component type, changing how it is inserted, discovered, and used |
| Compatible with ADR-004's Mix law as decided | Yes, unchanged: `x[n]` is exactly the mono dry input the law already assumes | Requires an explicit input-reduction step (sum-to-mono, or another rule) before the existing law applies; the law itself is unchanged only if reduction happens upstream of it | **Contradicts it.** ADR-004's Mix crossfades a live dry input against wet output in both channels; a generator has no dry input signal to crossfade against, so Mix as currently decided cannot apply unmodified |
| Engineering cost from current code | None — this is the DS-B route's exact current shape | One new reduction step (e.g., `(inL+inR)*0.5` or a two-tap variant) ahead of the unmodified mono diffusion chain; does not touch `src/dsp/` internals | Would require redesigning or discarding the Mix/dry-input contract ADR-004 decided and ADR-006 extended; effectively a different product than the one every accepted ADR since ADR-004 has assumed |
| Stereo information from the host input | Discarded before it reaches the wrongly-typed input (a stereo host source folds to whichever single channel the wrapper reads, or must itself be summed by wrapper code outside `src/dsp/`) | Preserved as a sum; input stereo *image* (not just level) is still discarded unless a genuinely stereo diffusion path is later built (ADR-006 (g)'s deferred "second input diffusion chain") | N/A — no input exists to preserve |
| Known measured asymmetry (DS-9) relevance | Directly exposed to the host with no correction | Same asymmetry, now on a reduced-to-mono host source rather than the wrapper's own test signal — unchanged by this alternative | N/A |
| What would have to be re-decided to adopt it | Nothing beyond this ADR | A new, currently-undecided reduction rule (sum-to-mono is the cheap default; true stereo diffusion is deferred and would require ADR-006 (g)'s named "second input diffusion chain with its own delay set," which is out of this ADR's scope) | ADR-004's Mix/dry contract, effectively a new parameter ADR |

**Finding, not a selection.** (C) is ruled out on a grounded engineering fact,
not a product preference: the accepted, unmodified ADR-004 Mix law requires a
live dry input signal, and ADR-006 extended that law into both output
channels without changing it. A no-input generator has no such signal.
Adopting (C) would require reopening and rewriting ADR-004's parameter
contract, which is out of this ADR's scope and is not recommended.

Between (A) and (B), no engineering fact in this repository forced one
answer — it was a genuine **product-scope choice**, because it depends on
how the shipped reverb is meant to be used (always inserted on a stereo
channel strip vs. also fed from mono sources; whether host-insert-slot
compatibility with common iOS DAWs matters more than avoiding an extra
reduction step). The table above narrowed the field to these two and
recorded their tradeoffs; the owner selected **(B)** (see "Decision and
tradeoffs" §1 and "Remaining decisions and later evidence").

## Decision and tradeoffs

1. **Bus channel-count layout: decided as alternative (B) —
   stereo-in/stereo-out with a sum-to-mono reduction ahead of the
   unmodified mono diffusion chain.** The owner selected (B) over (A) to
   match conventional AUv3 host insert-slot expectations, accepting the
   sum-to-mono reduction's engineering cost and its loss of input stereo
   *image* (not just level) as the tradeoff. **Decided (2026-09-19):
   sum-to-mono is an explicit interim step, not a permanent product
   behavior** — a true stereo-diffusion successor to ADR-006 (g)'s
   deferred "second input diffusion chain" is a real intended follow-on,
   not merely a hypothetical (see "Remaining decisions and later
   evidence" for what scoping that successor would require). Alternative
   (C), no-input generator, is rejected on the grounded engineering basis
   that it contradicts ADR-004's accepted dry-input Mix contract without a
   new parameter ADR.

2. **Dry/bypass policy, at decision level:**
   - On engaging `AUAudioUnit.shouldBypassEffect`, the wrapper's output must
     be the unprocessed dry input (bit-exact passthrough), matching the host
     expectation that a bypassed effect does not alter audio. This is the
     same endpoint ADR-004 already proves bit-exactly reachable via
     `Mix = 0` (`docs/testing.md`'s `testSetMixToZeroBypassesToDryExactly`),
     but AU-level bypass and `Mix = 0` are **not decided here to be the same
     mechanism**: `Mix = 0` still runs the full diffusion/FDN chain to
     produce a wet signal it then discards, while AU bypass may or may not
     need to keep running that chain. **Decided (2026-09-19): a hybrid
     approach.** The wrapper keeps computing the wet path after bypass
     engages, but only while state is provably non-silent — tracking
     elapsed samples since the last non-silent input against the
     closed-form `T_silence` bound (ADR-003 (b)(6)/(h)) for the currently
     configured Decay/Damp — and stops rendering once that bound is
     passed. This preserves tail continuity for a bypass toggle inside the
     tail window while avoiding indefinite CPU cost for toggles well after
     the tail has actually reached silence. **This is new logic beyond
     anything already implemented or decided elsewhere** — no existing
     code tracks elapsed-since-input against `T_silence` — so the bounded
     implementation plan must design and test it as a first-class feature,
     not assume it falls out of the existing `reset()`/fault-recovery
     contract. See "Design note — hybrid bypass mechanism" below for the
     state machine and the still-unverified closed-form bound.
   - **Wet-tail-on-bypass is bounded, not undefined or infinite.** ADR-003's
     proof that every recursive memory reaches exactly zero within a
     computable `T_silence` (a function of the configured decay/damping
     coefficients, via the `ε = 1e-20` cutoff) means any tailTime value the
     wrapper reports to the host is a *derivable, finite* quantity for the
     currently configured Decay/Damp settings, not a guess and not
     "infinite." `docs/testing.md` DS-10's measured figure (321,914 samples
     @48kHz for its declared fixture) is one concrete instance of this
     bound, not a fixed constant — the design requires tailTime to be
     recomputed (or bounded conservatively) whenever Decay/Damp change,
     because `T_silence` depends on them.
   - **Un-bypassing must not introduce a discontinuity that ADR-004's
     existing 20ms coefficient-ramp transport is designed to prevent
     elsewhere.** At decision level: any bypass/un-bypass transition should
     route through the same kind of bounded, ramped transition ADR-004
     already established for Mix, rather than an instantaneous switch of the
     AU's output source. This ADR does not specify the mechanism (that is
     implementation, out of scope) — it states that un-bypass must not be a
     hard discontinuity given that ADR-004's ramp discipline already exists
     for exactly this class of problem.
   - **Explicitly deferred (2026-09-19), not decided:** whether a
     host-exposed "kill the wet tail instantly on bypass" option should
     exist, as distinct from the hybrid default above. The owner declined
     to decide this now rather than leaving it accidentally open; it
     remains a genuine product decision to be made once UI is scoped —
     see "Remaining decisions."

3. **Buffer-aliasing policy: `DiffusionStereoPath::process` itself still
   cannot be called in-place, and the AU-facing bus's own in-place safety
   under item 1's now-decided alternative (B) is a new, unresolved
   question this ADR does not answer.** `DiffusionStereoPath::process`
   takes a 1-channel input and a 2-channel output as three independent
   pointers — this did not change when the owner selected (B), because (B)
   sums a *symmetric stereo AU-facing bus* down to mono before feeding the
   existing, unmodified mono-input core; the core function's own
   channel-count mismatch, and the impossibility of one input channel's
   storage simultaneously being two output channels' storage, are
   unaffected. **`DiffusionStereoPath::process` must therefore always be
   called with distinct input/output buffers, regardless of item 1's
   decision.** What item 1's acceptance newly opens is a *separate*
   question this ADR does not resolve: whether the AU's own public
   `canProcessInPlace` can report true for its now-symmetric stereo-in/
   stereo-out bus, given that the wrapper's sum-to-mono reduction step
   must read both input channels at a sample index before the core writes
   any output at that index. That depends on how the reduction step and
   the core call are sequenced in a future implementation, which this ADR
   has not designed. If a future true stereo-diffusion successor to
   ADR-006 (g) is built instead of the sum-to-mono interim step, and its
   implementation preserves `DiffusionStereoPath::processOne`'s
   read-before-write, no-lookahead per-sample discipline (confirmed by
   reading the source) across all channels, in-place aliasing would become
   straightforwardly safe at that point — this is stated as a requirement
   on that future implementation, not a claim that any current code
   already supports it. The bounded implementation plan that eventually
   builds item 1's decision must resolve `canProcessInPlace`'s actual
   value; this ADR only establishes what is and is not already known.

   **Recommended (2026-09-19), pending implementation-time confirmation:
   `canProcessInPlace = true`**, contingent on the wrapper performing its
   sum-to-mono reduction per-sample — reading both input channels and
   writing both output channels at the same index before advancing to the
   next — rather than through a separately-populated scratch buffer. This
   is grounded in `DiffusionStereoPath::processOne`'s confirmed
   read-before-write, no-lookahead per-sample discipline (established
   above); it does not itself satisfy the bounded implementation plan's
   required confirmation, which must still verify this holds for whatever
   it actually builds.

4. **This ADR does not promote DS-B's tap/diffusion parameters into a
   product configuration.** ADR-006's own boundary language is preserved
   verbatim in intent: the selected topology, injection vector, tap
   assignment, and diffusion coefficients "remain an evaluation baseline,
   not a proved stereo or perceptual result," and nothing in this ADR
   authorizes reading DS-9's measured L/R asymmetry, or any other DS-1…DS-13
   figure, as an accepted product characteristic. The bounded implementation
   plan that eventually builds alternative (B) still inherits ADR-006's
   evaluation-only status for the mono-core DSP route it wraps until a
   separate ADR says otherwise.

5. **Variable- and zero-length render blocks: the required invariant is
   already met by the current core, and the wrapper must not regress it.**
   ADR-007 cites `AUInternalRenderBlock`/`AUParameterEvent` as establishing
   that AUv3 hosts may call the render callback with variable-length or
   zero-length blocks. Reading `DiffusionStereoPath::process` and
   `FeedbackDelayNetwork::process` confirms both already tolerate
   `count == 0` as a documented, tested no-op with no allocation and no
   state mutation, and DS-11 confirms bit-identical output across ragged
   block partitions. **The invariant this design requires from the DSP core
   — tolerate a zero-sample render call without state corruption or crash,
   and tolerate any nonzero block length without a fixed-size assumption —
   is already satisfied.** The obligation this places on the wrapper (not
   the core) is to preserve that property: it must not introduce a
   fixed-block assumption, a per-block allocation, or a block-size
   precondition of its own when adapting host callbacks to `process()`.

6. **No wrapper, UI, or code work is authorized by this ADR, even if
   accepted as written.** As with ADR-007, a separately authorized bounded
   implementation plan is required before any `.h`/`.cpp`/build-target work
   proceeds against the decisions above, and that plan must still resolve
   [ADR-008](ADR-008-parameter-event-bridge.md)'s parameter-event bridge,
   [ADR-010](ADR-010-device-lifecycle-matrix.md)'s device/host matrix, and
   the still-unassigned state-schema prerequisite, none of which this ADR
   touches.

## Design note — hybrid bypass mechanism (2026-09-19)

Specifies §2's hybrid mechanism at the design level: architecture and
state machine, no code, no new authorization. §2 already named this "new
logic... to design and test"; this note answers *how*, not whether.

**Ownership and state.** Per this ADR's own boundary — the DSP core has
no bypass concept; bypass is wrapper policy — this is a small
wrapper-owned state object, never a `DiffusionStereoPath`/
`FeedbackDelayNetwork` member: a `silenceBoundSamples_` value (recomputed
whenever bypass engages or Decay changes while bypassed), an
`elapsedSinceBypass_` sample counter, and a `Running`/`Stopped` flag.

**Trigger and lifecycle.**
1. On bypass engage: read the current normalized Decay via the
   `getAll()` read-back accessor ([ADR-011](ADR-011-state-schema.md)'s
   design note), compute `silenceBoundSamples_` from it (see "Closed-form
   bound" below), reset `elapsedSinceBypass_` to 0, enter `Running`.
2. While `Running` and bypassed: continue calling the DSP core with
   **zero input** — never the host's live, now-bypassed input, since real
   input would prevent the tail from ever reaching silence and defeat the
   entire premise — discard the output, and increment
   `elapsedSinceBypass_` by each call's frame count.
3. Once `elapsedSinceBypass_ >= silenceBoundSamples_`: stop calling the
   DSP core entirely (enter `Stopped`). This is now behaviorally
   identical to plain pause/freeze, but is only ever reached after state
   is *provably* exact zero (ADR-003's proof), so it carries none of the
   audible-discontinuity risk the plain pause/freeze alternative had.
4. If Decay changes while `Running` (a Host/UI write can still land
   while bypassed): recompute `silenceBoundSamples_` from the new value
   and restart `elapsedSinceBypass_` at 0. Restarting the count against a
   freshly recomputed bound is always at least as conservative as
   continuing the old count against a stale one.
5. On un-bypass: if still `Running`, resume normal live-input processing
   immediately — the tail was continuously live throughout. If
   `Stopped`, resume immediately too — state is exactly zero, so this is
   audibly identical to a live network already at rest.

**Closed-form bound — candidate derivation, NOT yet verified; named as a
bounded follow-up task, not decided here.** ADR-003 already proves
`T_silence ≤ (m_max/f_s)·ln(ε/‖s₀‖₂)/ln ρ` with `ρ = σ_max(ΓA) =
maxᵢgᵢ`, and states `maxᵢgᵢ` is set by the *shortest* line:
`maxᵢgᵢ = γ₀^{m_min}` (ADR-003, "quantifying the headroom" section; NS-4's
table). Combining this with `ParameterAutomation::publish()`'s existing
`t60Zero(decay)` closed form (already implemented; no new derivation
needed there) gives a candidate — **derived this session, cross-
referencing ADR-003's `ρ` formula and `publish()`'s existing code, but
NOT independently verified against `docs/phases/phase1-pt-plan.md`'s own
closed forms, and not yet decided:**

`T_silence(decay) ≈ (m_max/m_min) · T60_zero(decay) · log10(‖s₀‖₂/ε) / 3`

Two properties worth naming even before verification: (a) this bound is
**independent of Damp** — `ρ` is defined purely from the per-line gains
`Γ`, and the damping filters `Hᵢ(z)` never enter `‖ΓA‖₂` (ADR-003 states
`sup|Hᵢ|≤1` as a separate fact), so using the Damp-independent bound is
always safe regardless of the actual Damp setting, since damping can only
shorten the true decay, never lengthen it; (b) **the DS-10 measured
figure (321,914 samples @48kHz) cannot be reused as a universal
constant** — it was measured at the wrapper's *default* fixture
(Decay=0.5, `T60_0≈1.09s`), not at Decay=1 (the longest achievable
tail), so a fixed-constant version of this mechanism would silently
truncate the tail at higher Decay settings and must not be built that
way.

**This candidate formula requires a bounded, documentation-only
verification task before being treated as ground truth** — cross-
checking it against `docs/phases/phase1-pt-plan.md`'s actual closed
forms and confirming the `‖s₀‖₂` reference convention (NS-8's
"full-scale impulse"), the same kind of small owner-authorized research
task this project has already used elsewhere (e.g. ADR-010's
iOS-version research). Until that verification lands, this mechanism's
exact numeric bound is named, not decided, and the bounded
implementation plan must not code against the formula above without
that check.

## Verification note — `T_silence(decay)` closed form (2026-09-19)

Closes the verification task named above. Documentation-only: no code
inspected changes, no new authorization.

**The algebraic combination is confirmed exact, not approximate.**
Substituting `ParameterAutomation::publish()`'s actual, already-implemented
`t60Zero(decay)` (`src/dsp/ParameterAutomation.cpp`: `t60Zero = t60Min_ *
pow(t60Max_ / t60Min_, decay)`, matching `docs/phases/phase1-pt-plan.md`'s
`T60_0(d) = T60_min·(T60_max/T60_min)^d` verbatim) into ADR-003's own
`γ₀ = 10^(−3/(f_s·T60₀))` and `ρ = γ₀^{m_min}`, then into `T_silence ≤
(m_max/f_s)·ln(ε/‖s₀‖₂)/ln ρ`, the `f_s` and `ln 10` terms cancel exactly
(not approximately), leaving:

`T_silence(decay) = (m_max/m_min) · T60_zero(decay) · log10(‖s₀‖₂/ε) / 3`

— the design note's candidate, confirmed by hand and numerically (Python,
double precision) to 9+ significant figures at two independent operating
points (`T60_zero = 4s`: `81.7641310508s` both ways; `T60_zero =
1.093397417s`, the DS-10 default fixture: `22.3501724236s` both ways). The
design note's "`≈`" should read "`=`": this is an exact re-parameterization
of ADR-003's already-proven bound by `decay`, not a new approximation, and
carries the same proof ADR-003 already gives it. Both of the note's
Damp-independence and DS-10-figure-non-reusability observations hold
unchanged by this result.

**Units: `T_silence(decay)` is seconds, not samples.** ADR-003's own bound
is dimensioned in seconds (`m_max` samples divided by `f_s` samples/second),
and every `f_s` term cancels in the reduction above, so the candidate
formula returns seconds directly from `T60_zero(decay)` (also seconds) with
no remaining sample-rate dependence. **`silenceBoundSamples_` is therefore
`T_silence(decay) · f_s`, not `T_silence(decay)` itself** — an implementation
detail the design note's naming does not spell out, flagged here so the
bounded implementation plan does not code the two quantities interchangeably.

**`‖s₀‖₂` reference convention confirmed: `√N`.** NS-8's "full-scale impulse"
and `FeedbackDelayNetwork::processSample()`'s injection code (`src/dsp/
FeedbackDelayNetwork.cpp`: a unit-amplitude sample added identically into
every line's write value, uniform, no per-line scaling at injection) together
fix `‖s₀‖₂ = √N` for a network at rest before the impulse — `√8 ≈
2.8284271` for the ADR-005 `N = 8` fixture. This value does not affect the
algebraic equivalence above (both sides carry the same symbolic `‖s₀‖₂`),
but is the concrete number the implementation must use to compute an actual
`silenceBoundSamples_`.

**Numeric sanity check against already-recorded measurements.** Converting
`T_silence(decay)·f_s` to samples with `‖s₀‖₂ = √8`, `ε = 1e-20`,
`m_min = 1297`, `m_max = 3889` (48kHz, ADR-005 fixture): at `T60_zero = 4s`
(NS-8's own fixture), the bound evaluates to ≈3,924,678 samples against
NS-8's measured 1,177,358 — a valid, conservative (never-violated,
not-tight) upper bound, consistent with `docs/testing.md`'s own framing of
these bounds elsewhere. At `T60_zero = 1.093397417s` (DS-10's Decay=0.5
default fixture), the bound evaluates to ≈1,072,808 samples against DS-10's
measured 321,914 — the **same** ≈3.3x margin as the `T60=4s` case, which is
the expected consequence of `T_silence(decay)` being exactly linear in
`T60_zero(decay)` and is itself a small additional consistency check, not
new evidence about tightness.

**New scope caveat surfaced by this verification, not previously named: the
formula bounds only the FDN's own internal state, not the whole
`DiffusionStereoPath` "DSP core" §2's hybrid mechanism actually wraps.**
ADR-003's `ρ = maxᵢgᵢ` is defined purely from the FDN's own `Γ`; it says
nothing about the input- and output-diffusion allpass sections' own memory.
`docs/testing.md`'s DS-10 correction round already establishes those two
diffusion cascades' settling drains, chained **additively, not maxed**, with
the FDN's own drain, "because each stage's own zero-input clock starts only
once the stage before it has itself fully drained." **This follow-up is now
closed** (2026-09-19), by mirroring `testDs10ProofTemplateAndMeasuredSilence`'s
exact formulas (`fdnLoopGainLogBound`, `fdnPoleRadiusBound`,
`stageDrainCircuits`, `dampingStateDrainSamples`,
`inputCascadeAbsoluteDrain`, `chainedOutputDrain` — all in
`tests/DiffusionStereoPathTests.cpp`) independently in Python and evaluating
them at four points across the full `Decay ∈ [0,1]` range (`T60_zero ∈
{0.006408s, 1.093397417s, 4.0s, 186.560s}`, 48kHz, `Damp=0` throughout so
`aᵢ=0` matches the only fixture this methodology has real evidence for).
The mirror reproduces the recorded `T60_zero=4s` figures (input-cascade
drain 87,841; output-cascade drain 92,494; FDN drain 5,839,909 vs. recorded
5,839,906; whole-chain 5,932,403 vs. recorded 5,932,400 — the handful-of-
samples difference is float32-vs-double coefficient rounding, not a
methodology divergence), which validates the mirror before trusting its
other three points:

| `Decay` | `T60_zero` | input-cascade drain | FDN drain | output-cascade drain | whole-chain |
|---|---|---|---|---|---|
| 0 (`T60_min`) | 0.006408s | 87,841 | 99,221 | 75,646 | 174,867 |
| 0.5 (wrapper default) | 1.093397417s | 87,841 | 1,603,313 | 88,750 | 1,692,063 |
| — (test fixture) | 4.0s | 87,841 | 5,839,909 | 92,494 | 5,932,403 |
| 1 (`T60_max`) | 186.560s | 87,841 | 298,359,360 | 102,095 | 298,461,455 |

**Result, corrected from the prior note's assumption: the input cascade is
exactly `Decay`-independent, but the output cascade is not.**
`inputCascadeAbsoluteDrain` is confirmed **exactly constant (87,841 samples
@48kHz)** across the entire range — expected from inspection alone, since
its formula (`docs/decisions/ADR-009...` design note's own citation of
`tests/DiffusionStereoPathTests.cpp`'s loop) never references any
FDN-derived quantity (`rho`, `mu`, `fdnDecayRate`), only the fixed input
delay lengths and the fixed stored allpass coefficient `q`. The prior note's
claim that the **output** cascade drain is likewise a fixed constant is
**wrong, corrected here**: `outputChainDrain` depends on `Decay` through
`qMax → mu, fdnDecayRate → rho`, and ranges from 75,646 to 102,095 samples
(48kHz) across the full range — a real ≈35% swing, not a constant, though
small in absolute terms (≤ 0.6s) and dominated everywhere in this table by
the FDN's own drain, which itself spans over three orders of magnitude
(99,221 to 298,359,360) across the same range. **The correct general
`silenceBoundSamples_(decay)` for the actual wrapped `DiffusionStereoPath`
is `inputCascadeDrainSamples + T_silence(decay)·f_s +
outputCascadeDrainSamples(decay)`**, chained additively per the rule above;
using the fixed `Decay=1` worst-case value of `outputCascadeDrainSamples`
(≈102,095 @48kHz — not yet computed at 44.1kHz) as a conservative constant
is a defensible simplification for an implementation plan, but should be
recorded as an approximation, not an exact invariant, if taken.

**A second, independent finding from this same numeric exercise, useful but
outside this follow-up's original question:** the FDN-drain figures this
table computes via the more elaborate per-line `rStar`/`mu`/`qMax` method
(the one `testDs10ProofTemplateAndMeasuredSilence` already uses) are
**consistently looser (larger) than this ADR's own simpler, already-verified
`T_silence(decay)` candidate** — by a roughly constant ≈1.5x across all four
points (e.g. at `T60_zero=4s`: 5,839,909 vs. `T_silence(4s)·f_s ≈
3,924,678`; at `T60_zero=186.56s`: 298,359,360 vs. `≈183,067,000`). Both are
independently-derived, valid, conservative (never-violated) upper bounds on
the FDN's own state; they simply come from different proof techniques
(global operator-norm vs. per-line pole-radius), so neither is required to
dominate the other, and this ADR's simpler formula happening to be the
tighter one of the two makes it the more practical choice for the FDN-only
term of `silenceBoundSamples_`, not a discrepancy needing resolution.

**A third finding, a test-code gap rather than a math error:** the existing
`fdnWriteDrain` search in `tests/DiffusionStereoPathTests.cpp` is an
explicit linear loop capped at 200,000,000 iterations. At `Decay=1`
(`T60_zero=186.56s`) it needs **≈298,267,629** iterations — this Python
mirror only completed that point using a closed-form replacement (the
underlying quantity is a plain geometric decay; the loop is not load-bearing
math, just how the test happens to compute it). If
`testDs10ProofTemplateAndMeasuredSilence` were ever parameterized to run at
`Decay=1` instead of its current single hardcoded `T60_zero=T60_pi=4.0`
fixture, it would hit this cap and report a spurious convergence failure,
not a real one. Named here as a latent test-code limitation, not a defect in
any currently-recorded evidence (the test has never actually been run at
that fixture), and not something this documentation-only task fixes.

## Remaining decisions and later evidence

Using this project's established phrasing pattern (see ADR-007's "Remaining
decisions and later evidence"): before any wrapper implementation plan that
depends on this ADR, the owner must resolve —

- **Effect bus channel-count layout: decided (2026-09-18) as alternative
  (B), stereo-in/stereo-out with a sum-to-mono input reduction.** (The
  no-input generator alternative (C) was never open — it was rejected on
  the grounded engineering basis stated in §1 and §Alternatives.)
- **Decided (2026-09-19): sum-to-mono input reduction is an explicit
  interim step**, not a permanent product behavior — see §1. A true
  stereo-diffusion successor (ADR-006 (g)'s deferred "second input
  diffusion chain with its own delay set") is a real intended follow-on;
  scoping and authorizing that successor remains separate, later work.
- **Recommended (2026-09-19), pending implementation-time confirmation:
  `canProcessInPlace = true`** — see §3 for the source grounding and the
  required per-sample-interleaved implementation constraint. The bounded
  implementation plan must still confirm this holds for whatever it
  actually builds, since `DiffusionStereoPath::process` itself must always
  be called with distinct buffers regardless.
- **Decided (2026-09-19): a hybrid approach to the bypass
  CPU-vs-tail-continuity tradeoff** — see §2 and the "Design note —
  hybrid bypass mechanism" section for the full state machine. Keep
  computing the wet path only while state is provably non-silent
  (elapsed-since-bypass tracked against the closed-form `T_silence`
  bound), stopping once past it. This is new logic the bounded
  implementation plan must design and test as a first-class feature; §2
  states the bounded-tail-time and bit-exact-passthrough requirements it
  must still satisfy. **The design note's `T_silence(decay)` closed form
  is now verified (2026-09-19)** as an exact reduction of ADR-003's own
  proven bound — see "Verification note" above for the algebra, the
  `‖s₀‖₂ = √N` convention, the seconds-vs-samples unit correction, and the
  numeric cross-checks against NS-8/DS-10's recorded measurements. That
  verification surfaced a real, previously-unnamed scope gap — the formula
  bounds only the FDN's own state, not the whole `DiffusionStereoPath` the
  mechanism actually wraps — which the follow-up cascade-drain task (also
  2026-09-19, same "Verification note" section) has now closed: the input
  cascade's drain is confirmed exactly `Decay`-independent (87,841 samples
  @48kHz, constant across the full range), but the output cascade's drain is
  **not** — it varies ≈35% (75,646 to 102,095 samples @48kHz) across
  `Decay ∈ [0,1]`, correcting the prior note's assumption that both were
  fixed. The implementation plan must sum `inputCascadeDrainSamples +
  T_silence(decay)·f_s + outputCascadeDrainSamples(decay)` (or the
  `Decay=1` worst case of the last term, as a documented approximation) —
  not `T_silence(decay)·f_s` alone.
- **Explicitly deferred (2026-09-19), not decided:** whether a host-exposed
  "kill the wet tail instantly on bypass" affordance should exist, as a
  distinct, user-selectable behavior from the hybrid default in §2. The
  owner deferred this until UI is scoped rather than deciding it now — an
  explicit product/UX decision, not an engineering necessity.
- **Decided (2026-09-19): multi-channel/surround output is not a target.**
  Nothing in this ADR, ADR-006, or ADR-007 considers more than two output
  channels, and nothing is gained by deciding this preemptively. If
  multichannel/surround output is raised later, both this ADR's bus table
  and ADR-006's tap design would need to be reopened from scratch.
- **Whether the DS-B route's specific tap/diffusion parameters (or any
  successor stereo-diffusion design) are acceptable as heard once wrapped**,
  independent of this ADR — DS-9's measured asymmetry and DS-7's conditional
  coherence claim remain uninterpreted pending review (`docs/testing.md`,
  "Known open items"), and this ADR does not adjudicate them.
- **The 44.1kHz DS-8 evidence gap** (`docs/testing.md`: "DS-8 is measured at
  48kHz only; a 44.1kHz leg is a small follow-up") should be closed before
  any production dry/wet-mix or mono-fold-down claim is made at the second
  fixture rate; this ADR does not close it.

As of 2026-09-19, the bus channel-count layout, the sum-to-mono interim
label, the hybrid bypass CPU-vs-tail approach, the `canProcessInPlace`
recommendation, and the multi-channel/surround non-target decision are
resolved (see the dated notes above). The kill-tail-on-bypass affordance
remains explicitly deferred until UI is scoped, and the DS-9/DS-7 sonic
acceptance items and the 44.1kHz DS-8 evidence gap remain open; none of
this ADR's acceptance stands in for the owner's sign-off on those three.

## Revisit when

A true stereo-diffusion successor to ADR-006 (g)'s deferred second input
chain becomes relevant, superseding the sum-to-mono interim reduction;
DS-9's L/R asymmetry or DS-7's coherence finding is reviewed and judged
unacceptable, which would reopen ADR-006's tap design and, with it, this
ADR's assumption that the wrapped route is a fixed evaluation baseline;
ADR-008's parameter-event bridge decision lands and turns out to constrain
bypass-transition timing in a way this ADR did not anticipate; a supported
sample rate or decay range is added that changes the `T_silence` bound's
order of magnitude enough to affect a reported tailTime's practicality; the
owner decides multi-channel/surround output is a target; or real host/device
evidence (ADR-007's still-outstanding verification plan) shows a bypass or
in-place assumption recorded here does not hold in practice on a named host.
