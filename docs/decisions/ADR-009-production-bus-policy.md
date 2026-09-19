---
id: "ADR-009"
status: proposed
implementation: "none; architectural decision only, no wrapper/UI/dependency code"
review: "pending owner review"
review_document: null
depends_on: [ADR-001, ADR-003, ADR-004, ADR-006, ADR-007]
---

# ADR-009 — Production audio bus layout, dry/bypass, and buffer-aliasing policy

## Review summary

- **Proposal:** decide the production AUv3 bus shape's engineering constraints, a
  dry/bypass and wet-tail policy at decision level, and a buffer-aliasing
  policy for the wrapper — without authorizing any wrapper, UI, or dependency
  code.
- **Why:** ADR-007 named production bus layout, dry/bypass behavior, and
  buffer-aliasing policy as open prerequisites to any wrapper implementation
  plan, and stated plainly that "DS-B's mono-to-stereo route does not decide
  these." Nothing in the codebase currently answers them.
- **Consequence:** the input/output *channel-count* bus shape and several
  concrete bypass/tail behaviors are recorded here as owner-gated product
  choices, not engineering facts — this ADR narrows the option set and states
  the tradeoffs, it does not select for the owner. Buffer-aliasing policy and
  the zero/variable-block invariant *are* decided here as grounded technical
  findings from the current source.
- **Uncertainty:** whether the product ships as mono-in/stereo-out or
  stereo-in/stereo-out, how a stereo input would be reduced or routed into the
  existing mono diffusion chain, and what a host-exposed "kill tail on
  bypass" control should do are all unresolved and require explicit owner
  sign-off (see "Remaining decisions").

**Status: Proposed — architectural decision only, no implementation.** This
record authorizes no wrapper, UI, dependency, or other implementation. Even if
accepted as written, a separately authorized bounded implementation plan is
still required before any code is written against it, exactly as ADR-007's
acceptance did not authorize a wrapper plan on its own.

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

Between (A) and (B), no engineering fact in this repository forces one
answer — it is a genuine **product-scope choice**, because it depends on how
the shipped reverb is meant to be used (always inserted on a stereo channel
strip vs. also fed from mono sources; whether host-insert-slot compatibility
with common iOS DAWs matters more than avoiding an extra reduction step).
This ADR does not select between (A) and (B) on the owner's behalf; it
narrows the field to these two and records their tradeoffs. See "Remaining
decisions."

## Decision and tradeoffs

1. **Bus channel-count layout remains an explicit owner decision (see
   "Remaining decisions"), narrowed to alternatives (A) and (B) above.**
   Alternative (C), no-input generator, is rejected on the grounded
   engineering basis that it contradicts ADR-004's accepted dry-input Mix
   contract without a new parameter ADR. This ADR does not pick (A) or (B).

2. **Dry/bypass policy, at decision level:**
   - On engaging `AUAudioUnit.shouldBypassEffect`, the wrapper's output must
     be the unprocessed dry input (bit-exact passthrough), matching the host
     expectation that a bypassed effect does not alter audio. This is the
     same endpoint ADR-004 already proves bit-exactly reachable via
     `Mix = 0` (`docs/testing.md`'s `testSetMixToZeroBypassesToDryExactly`),
     but AU-level bypass and `Mix = 0` are **not decided here to be the same
     mechanism**: `Mix = 0` still runs the full diffusion/FDN chain to
     produce a wet signal it then discards, while AU bypass may or may not
     need to keep running that chain. Whether bypass continues computing the
     wet path in the background (to preserve a re-enterable tail, at full
     CPU cost) or pauses/freezes it (saving CPU, at the cost of losing tail
     continuity) is a genuine engineering/product tradeoff this ADR does not
     resolve — see "Remaining decisions."
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
   - Whether a host-exposed "kill the wet tail instantly on bypass" option
     should exist at all (as distinct from the default tail-preserving
     behavior above) is a genuine product decision — see "Remaining
     decisions."

3. **Buffer-aliasing policy: in-place processing cannot be declared safe
   under a mono-in/stereo-out bus, and this is a grounded finding, not a
   guess.** `DiffusionStereoPath::process` takes a 1-channel input and a
   2-channel output as three independent pointers. Apple's conventional
   in-place optimization for an effect AU assumes the *same* buffer list
   serves as input and output with matching channel counts per bus; a
   mono-in/stereo-out shape cannot satisfy that assumption because one input
   channel's storage cannot simultaneously be two output channels' storage.
   **`canProcessInPlace` must therefore report false (or the wrapper must
   force distinct buffers) for as long as the production bus stays
   asymmetric in channel count — i.e., this constraint is a direct
   consequence of item 1's alternative (A), not independent of it.** If
   item 1 instead selects alternative (B), or a future true
   stereo-diffusion successor is adopted, with matching channel counts,
   the current implementation's read-before-write, no-lookahead per-sample
   discipline (confirmed by reading `DiffusionStereoPath::processOne`) is
   *compatible* with per-channel in-place aliasing, provided any future
   per-block implementation preserves that same discipline (read every
   input channel at sample index `i` fully before writing any output
   channel at index `i`) — this is stated as a requirement on a future
   implementation, not a claim that any current code already supports a
   stereo bus.

4. **This ADR does not promote DS-B's tap/diffusion parameters into a
   product configuration.** ADR-006's own boundary language is preserved
   verbatim in intent: the selected topology, injection vector, tap
   assignment, and diffusion coefficients "remain an evaluation baseline,
   not a proved stereo or perceptual result," and nothing in this ADR
   authorizes reading DS-9's measured L/R asymmetry, or any other DS-1…DS-13
   figure, as an accepted product characteristic. A future bounded
   implementation plan choosing alternative (A) or (B) still inherits
   ADR-006's evaluation-only status for the DSP route it wraps until a
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

## Remaining decisions and later evidence

Using this project's established phrasing pattern (see ADR-007's "Remaining
decisions and later evidence"): before any wrapper implementation plan that
depends on this ADR, the owner must resolve —

- **Effect bus channel-count layout**: mono-in/stereo-out (as DS-B stands,
  alternative (A)) versus stereo-in/stereo-out with a sum-to-mono input
  reduction (alternative (B)). This is a product-scope choice about target
  host insert conventions and expected source material, not an engineering
  fact; §2's table narrows it but does not resolve it. (The no-input
  generator alternative (C) is not open — it is rejected on the grounded
  engineering basis stated in §1 and §Alternatives.)
- **If (B) is selected**, whether sum-to-mono input reduction is an
  acceptable permanent product behavior, or whether it is only an interim
  step pending a true stereo-diffusion successor (ADR-006 (g)'s deferred
  "second input diffusion chain with its own delay set") — a sonic/scope
  decision, not this ADR's to make.
- **Bypass CPU-vs-tail-continuity tradeoff**: whether `shouldBypassEffect`
  pauses/freezes the DSP core (saving render cost, at the cost of losing
  tail continuity across a bypass toggle) or keeps it running silently in
  the background (preserving a re-enterable tail, at full CPU cost while
  bypassed). §2 states the bounded-tail-time and bit-exact-passthrough
  requirements either approach must satisfy; it does not choose between
  them.
- **Whether a host-exposed "kill the wet tail instantly on bypass"
  affordance should exist** as a distinct, user-selectable behavior from the
  tail-preserving default in §2 — an explicit product/UX decision, not an
  engineering necessity.
- **Whether multi-channel/surround output is ever a target.** Nothing in
  this ADR, ADR-006, or ADR-007 considers more than two output channels;
  if surround or multichannel output becomes a target, both this ADR's bus
  table and ADR-006's tap design would need to be reopened.
- **Whether the DS-B route's specific tap/diffusion parameters (or any
  successor stereo-diffusion design) are acceptable as heard once wrapped**,
  independent of this ADR — DS-9's measured asymmetry and DS-7's conditional
  coherence claim remain uninterpreted pending review (`docs/testing.md`,
  "Known open items"), and this ADR does not adjudicate them.
- **The 44.1kHz DS-8 evidence gap** (`docs/testing.md`: "DS-8 is measured at
  48kHz only; a 44.1kHz leg is a small follow-up") should be closed before
  any production dry/wet-mix or mono-fold-down claim is made at the second
  fixture rate; this ADR does not close it.

None of the above is resolved by this ADR's acceptance. Acceptance narrows
the option set and states the engineering constraints; it does not stand in
for the owner's sign-off on any bulleted item.

## Revisit when

Alternative (A) or (B) is selected by the owner and a true stereo-diffusion
successor to ADR-006 (g)'s deferred second input chain becomes relevant;
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
