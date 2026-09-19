---
id: "ADR-008"
status: proposed
implementation: "none; design-only, no bridge/wrapper/UI/dependency code"
review: "pending owner review"
review_document: null
depends_on: [ADR-001, ADR-004, ADR-007]
---

# ADR-008 — Nonblocking parameter-event bridge for host/UI-to-render-thread control delivery

## Review summary

- **Proposal:** a single non-render "Bridge Controller" task becomes the
  sole caller of [ParameterAutomation](../../src/dsp/ParameterAutomation.h)'s
  `setDecay`/`setDamp`/`setMix` (and
  [DiffusionStereoPath](../../src/dsp/DiffusionStereoPath.h)'s forwarding
  equivalents). Three named, fixed producer roles — Host, UI, StateRestore —
  never call those setters directly; each writes only a raw normalized
  `[0,1]` value into a small set of fixed, wait-free single-slot mailbox
  cells, one cell per (parameter, producer) pair, that the Controller polls
  and applies.
- **Why:** the setters' derivation is off-render-thread-only and
  single-writer by contract; multiple concurrent producers (an AU host's
  render-thread-delivered parameter events, UI/`AUParameterObserver`
  writes, and a future state-restore apply) must be serialized into that
  single-writer contract without ever blocking, allocating or locking on
  the render thread, and without inventing whole-coefficient-set atomicity
  this project has already shown it does not need.
- **Consequence:** render-thread work per callback is bounded to scanning
  the host-delivered event list once and performing at most three wait-free
  mailbox writes (each a relaxed value store plus a release generation
  increment); the existing `checkForNewTargets()`/`advance()`
  block-boundary consumption in `ParameterAutomation` is unchanged. Host
  timing (`eventSampleTime`, `rampDurationSampleFrames`) is flattened to
  "next block boundary," per ADR-004(c)/(d), not honored sample-accurately.
- **Uncertainty:** cross-producer tie-break order, a hard per-callback event
  cap, the Controller's scheduling mechanism/priority (hence true online
  publication latency), and whether single-slot last-write-wins coalescing
  is perceptually acceptable under dense automation are none of them
  settled by this design; they are named below as owner-gated.

**Status: Proposed.** This record authorizes no bridge, wrapper, UI,
dependency, or other implementation. It does not choose a state-restore
schema (a separate, later ADR owns versioning/persistence/restore
semantics) — it only names the producer slot that ADR reuses. A separately
authorized bounded implementation plan is still required before any code,
exactly as [ADR-007](ADR-007-auv3-integration-comparison.md) requires.

## Context and scope

[ADR-007](ADR-007-auv3-integration-comparison.md) accepted native Apple
AUv3 APIs as the framework direction and identified this bridge as a named
open prerequisite to any wrapper implementation plan, quoting its own
"Required parameter bridge decision" section in full. That section is
binding context here and is not restated in full; the load-bearing lines
are: `ParameterAutomation`'s setters are "explicitly not realtime safe and
single-writer"; "the existing atomic coefficient publication is not an AU
event adapter"; AUParameterTree callbacks, AU render events, or JUCE
parameter callbacks must not be wired directly to these setters; and this
design "must identify the single non-render coefficient writer; serialize
host, UI and state-restore producers; define event capacity, coalescing
and overflow behavior; bound work on every render callback; and define
timestamp handling, publication latency and determinism."

This ADR is scoped narrowly to that prerequisite. It does not choose an
AUv3 wrapper's lifecycle, buses, state schema, UI toolkit, or build/signing
path — those remain the open items ADR-007's "Remaining decisions and
later evidence" already lists. It does not revise
[ADR-004](ADR-004-parameters.md): sections (c) and (d) — block-boundary
target consumption and the fixed 20ms internal ramp — remain binding and
unchanged. It does not claim sample-accurate host automation or host-ramp
fidelity; that would require an explicit ADR-004 revision and a new
bounded-work argument, which this ADR is explicitly told not to attempt.
It does not design a state-persistence schema; it only names where a
future state-restore producer plugs into the serialization scheme decided
here, so that later ADR is not forced to redesign this bridge.

### What the current code actually does (the boundary this ADR sits above)

Read directly from source, not assumed:

- `ParameterAutomation::setDecay/setDamp/setMix(double normalized)`
  (`ParameterAutomation.cpp`) each: reject a non-finite input (incrementing
  `nonFiniteRejectionCount_`, leaving the published set/generation
  unchanged); clamp a finite out-of-range input to `[0,1]`; call `publish()`,
  which derives the full `2·lineCount+2` coefficient set in `double` using
  `pow`, `log10` (via the `t60Max_`/`t60Min_` closed forms computed at
  `prepare()`), `sqrt` and, for Mix, `cos`/`sin`; on success stores each
  derived value into a fixed `std::vector<std::atomic<float>>` with relaxed
  stores and then `generation_.fetch_add(1, std::memory_order_release)`.
  The header's own comment is explicit: "Not real-time safe... never
  called concurrently with itself (single-writer, per ADR-004 (d))."
  `docs/testing.md`'s PT-8 record independently confirms the cost is the
  derivation, not allocation: "zero allocation across both a no-change and
  a change-every-sample render (~2.3ms vs. ~8.3ms wall time... the
  difference is `set*()`'s double-precision derivation cost, not
  allocation)."
- `ParameterAutomation::checkForNewTargets()` acquire-loads the generation
  counter; if unchanged since `consumedGeneration_`, it performs exactly
  one atomic load and returns; if changed, it starts a fresh ramp toward
  each of the `2·lineCount+2` published targets. `advance()` steps every
  active ramp by one sample toward its target (with a bounded per-coefficient
  early-completion check, not a signal-value-dependent branch) and applies
  the result to the network. Both are `noexcept`, allocation-free, and
  their per-call work is bounded by `lineCount` (`FeedbackDelayNetwork::
  kMaxLineCount == 16`). **This ADR does not change either function or the
  transport between them and the render thread.** It only addresses what
  is allowed to call `setDecay`/`setDamp`/`setMix` in the first place, and
  how multiple producers get a value there safely.
- `DiffusionStereoPath::setDecay/setDamp/setMix` (`DiffusionStereoPath.h`)
  are "thin forwards" to the above, inheriting the identical contract:
  "Not real-time safe... never called concurrently with itself." Its
  `process()` observes automation "once at the block boundary," matching
  ADR-004(d)'s design.

The gap this ADR closes: today there is exactly one implicit, undocumented
producer (whatever test or future call site invokes `setDecay` et al.
directly). An AUv3 wrapper introduces at least three concurrent producer
*contexts* — a host delivering `AUParameterEvent`s inside the render
callback, a UI or `AUParameterObserver` write off the render thread, and a
future state-restore apply — none of which may call these setters
directly or concurrently with each other.

## Alternatives: producer→controller handoff mechanism

Three concrete mechanisms were compared for how producers get a pending
value to the single Bridge Controller (defined below) without blocking,
locking or allocating on the render thread, and specifically on the
render-thread-side host-intake path.

| Criterion | (A) SPSC ring buffer of timestamped event structs, one per producer role | (B) Per-(parameter, producer) single-slot mailbox: atomic `float` + atomic `uint64_t` generation — **recommended** | (C) Fixed triple-buffer of a full 3-parameter tuple per producer, published by index |
|---|---|---|---|
| Producer-side write cost | Bounded but variable: write value+timestamp, then release-store an advancing write index; wait-free only while capacity remains | **Fixed, O(1) always**: one relaxed float store + one release fetch-increment, identical in shape to `ParameterAutomation::publish()`'s own already-accepted pattern | O(1) writer must first snapshot the *other two* parameters' current values into the same buffer slot, or lose them — extra state the producer must track |
| Can the render-thread-side intake path overflow? | Yes — a bounded ring has a capacity, and dense host automation within one callback can fill it, requiring an explicit drop-oldest/drop-newest policy | **No, by construction** — there is nothing to overflow; a new write always occupies the single slot, so "overflow" and "coalesce" are the same event, not a distinct failure mode needing its own policy | No, for the same reason as (B), but only if every producer always publishes a complete 3-tuple, which reintroduces the extra state above |
| Coalescing/ordering guarantee | Preserves every distinct event and its arrival order, up to capacity | Preserves only the most recent value per (parameter, producer) pair; intermediate values between two Controller drains are lost, always, not just under load | Preserves only the most recent full tuple per producer; a change to one parameter can be lost if overwritten by a publish of the other two before the Controller reads it, unless the producer always republishes all three |
| Whole-set atomicity needed? | No more than (B) — same as ADR-004(d)'s own finding for the render-side transport (ADR-003(c): a torn coefficient combination is not a stability hazard) | **No** — each parameter's mailbox is independent, matching ADR-003(c)/ADR-004(b3)'s already-established result that per-control independence is sufficient here | Would only need it if a state-restore producer must apply an atomic 3-parameter preset; that requirement, if real, belongs to the later state-schema ADR, not to this transport choice |
| Code complexity | Highest: index wraparound, capacity choice, drop policy, and (for the render-thread producer) an unavoidable branch on "is the queue full" | **Lowest**: nine total cells (3 parameters × 3 producer roles), each a direct structural copy of a mechanism `ParameterAutomation` already implements and this project has already tested (PT-1..PT-9) | Comparable to (A) once the tuple-snapshot requirement is worked through; buffer-lifetime reasoning (which buffer a reader may still be touching) is the same class of complexity ADR-004(d) rejected (d2) for |
| Dense-automation fidelity | Best of the three — no event is silently dropped short of true overflow | **Worst** — every intermediate value between two Controller drains is unconditionally discarded, by design, not only under contention | Same coalescing loss as (B), plus the tuple-snapshot correctness burden |
| Consistency with an already-accepted project pattern | New concept for this project | **Directly generalizes ADR-004(d)'s accepted (d1)** — "per-coefficient atomic array + generation counter" — to multiple producer roles feeding the same downstream mechanism | Structurally closest to ADR-004(d)'s *rejected* (d2) (double/triple-buffered block, publish-by-index), rejected there for buffer-lifetime reasoning this ADR would have to re-argue for no proven benefit |

**Recommendation: (B).** It is the only option with a strictly bounded,
allocation-free, branchless-on-the-hot-path producer write, it cannot
overflow because it has no queue depth to exceed, and it is not a new
mechanism for this codebase — it is the same atomic-target-plus-generation
idea ADR-004(d) already chose and PT-1..PT-9 already exercise, replicated
per producer role instead of invented fresh. Its cost is honest and named,
not hidden: **it always throws away every value but the last one written
between two Controller drains**, for every producer, unconditionally. That
tradeoff — trading dense-automation fidelity for the simplest, most
provably-bounded mechanism — is flagged explicitly in Remaining Decisions
below; it is a product/perceptual judgment call, not an engineering
necessity, in the same way ADR-004 flagged `D_max` and `τ_ramp` as
requiring testing.md's Sonic acceptance gate rather than settling them by
proof. (A) is named as the explicit successor if dense-automation fidelity
is later required — mirroring how ADR-004(d) itself named its rejected
(d3) SPSC-queue alternative as the successor "if host automation fidelity
... makes block quantization the dominant time granularity." (C) is
rejected outright: it buys nothing (B) does not already give at lower
complexity, for the same reason ADR-004(d) rejected its own (d2).

## Decision and tradeoffs

### 1. The single non-render coefficient writer

**A single, non-reentrant Bridge Controller task is the only permitted
caller of `ParameterAutomation::setDecay`/`setDamp`/`setMix` (and
`DiffusionStereoPath`'s forwards).** No AUParameterTree callback, AU render
event, `AUParameterObserver` callback, or state-restore code path calls
these setters directly, under any circumstance — this is the literal
requirement ADR-007 states ("Do not wire AUParameterTree callbacks, AU
render events or JUCE parameter callbacks directly to these setters") and
this ADR treats it as a hard invariant, not a preference.

Concretely, this resolves as **both** halves of the question this ADR was
asked to settle, because they are complementary, not exclusive:

- **Intake can happen on the render thread** for host-delivered
  `AUParameterEvent`s, because that is the only context in which they
  arrive (inside `internalRenderBlock`). Render-thread intake does the
  minimum possible work: read a raw normalized value and a parameter
  identifier, and write them into a mailbox cell (§2). It never calls a
  setter itself.
- **The actual setter call — the not-realtime-safe derivation — is always
  deferred** to the Bridge Controller, which runs on a dedicated task that
  is never the render thread and is never invoked reentrantly.

For the **online** (real AUv3 host) case, the Bridge Controller is a
single serial task — a dedicated low-overhead thread or a strictly serial
dispatch queue, mechanism deferred to the implementation plan — woken
whenever a producer writes a mailbox, or on a bounded polling cadence, and
never running concurrently with itself. For the **offline** (this
project's own WAV renderer, or repeatable test/render fixtures) case, the
Controller's drain-and-apply step is not a background task at all: it is
invoked **synchronously**, once per processed block, from the same thread
driving the offline render loop, immediately before that block's
`checkForNewTargets()`/`advance()` calls. This distinction is what lets
§6 make a real determinism claim instead of an aspirational one.

### 2. Handoff mechanism, capacity, coalescing, overflow

Per the recommendation above: **nine fixed mailbox cells**, one per
(parameter ∈ {Decay, Damp, Mix}) × (producer role ∈ {Host, UI,
StateRestore}). Each cell is exactly `{ std::atomic<float> value;
std::atomic<uint64_t> generation; }`, matching `ParameterAutomation`'s own
existing transport shape field-for-field.

- **Capacity**: one pending value per cell — nine values total, fixed at
  compile time, never resized, never allocated after preparation.
- **Coalescing**: strict last-write-wins per cell, unconditionally, not
  only under load. A producer writes `value` (relaxed) then
  `generation.fetch_add(1, release)`, exactly as `ParameterAutomation::
  publish()` already does for its own generation counter. Two writes to
  the same cell before the Controller's next drain leave only the second
  visible; the first is not recoverable and is not counted or logged by
  this design (a diagnostic counter, if wanted, is an implementation
  detail, not a correctness requirement).
- **Overflow**: **structurally impossible**, because there is no queue
  depth to exceed — every new write occupies the same single slot. Given
  "nonblocking" is a hard render-thread constraint, this sidesteps the
  overflow question a bounded ring buffer would force (drop-oldest vs.
  drop-newest vs. block) by removing the possibility rather than choosing
  among its answers. This is the specific place where simplicity was
  chosen over preserving dense-automation fidelity — named again in
  Remaining Decisions, not hidden here.
- **Cross-producer conflicts**: if Host and UI (say) both have a pending
  value for the same parameter when the Controller drains, both cells are
  independently valid; the Controller applies them in a **fixed scan
  order** (proposed: Host, then StateRestore, then UI). Because
  `publish()` unconditionally overwrites, whichever role is scanned last
  in a given pass wins — so a live UI touch always wins over a same-pass
  Host or StateRestore value, on the reasoning that the most immediate
  user intent should not be silently discarded. This protects a restore
  from being overridden by a *stale* Host write already pending from an
  earlier pass (Host is scanned before StateRestore), but it does **not**
  protect a restore from a UI touch landing in the *same* pass — that
  case is left to whichever value the drain observes last, an accepted
  consequence of this order rather than a guarantee this ADR makes. This
  fixed order is a named product judgment, not a derived fact (Remaining
  Decisions). Applying the scan order and then calling `publish()` once
  per parameter per drain (resolving the tie-break first, publishing the
  single winning value) is the intended implementation; naively calling
  `publish()` once per pending role would run its `pow`/`log10`/`cos`/`sin`
  derivation up to three times per parameter per drain for no benefit,
  since PT-8 already shows that derivation, not allocation, is the
  dominant cost.

### 3. Render-thread-side work bound

Per render callback, on the render thread:

1. Iterate the host-supplied `AURenderEvent`/`AUParameterEvent` list for
   this callback exactly once (its length is set by the host, external to
   this design, and is not itself bounded here — see Remaining Decisions
   for a proposed hard cap). For each event, perform O(1) work: identify
   which of the three accepted parameters it targets and record its value
   into one of three local (stack) scratch variables, overwriting any
   value already recorded there for this same callback. No atomic
   operation, no allocation, and no branch on signal values occurs inside
   this per-event step.
2. After the scan, perform **at most three** wait-free mailbox writes
   (Host-role cells only) — one per parameter that changed in this
   callback, regardless of how many `AUParameterEvent`s targeted it. This
   is the mechanism's coalescing (§2) applied at intake as well as at
   drain: a burst of same-parameter events inside one callback collapses
   to the last one before it ever reaches a mailbox.
3. The existing `ParameterAutomation::checkForNewTargets()` and
   `advance()` calls are unchanged and continue exactly as today: at most
   one acquire-load when nothing changed, or `O(lineCount) ≤ 16`
   relaxed loads/ramp-starts when it did.

No lock, no allocation, and no unbounded-in-principle render-thread step
is introduced by this design; step 1's cost is proportional to
host-scheduled event count, which this ADR does not control and names as
an open item rather than asserting a proof it cannot make.

### 4. Timestamp handling

`AUParameterEvent` carries `eventSampleTime` and `rampDurationSampleFrames`
(cited already in ADR-004's Evidence section). This bridge reads neither
for scheduling purposes: it uses only the **relative order** in which
events appear in the host-delivered list (Apple documents these as
time-ordered) to decide which value is "last" for a given parameter within
one callback — the same last-write-wins rule §2/§3 already apply. The
resulting value is not applied at its `eventSampleTime` offset within the
block; it is applied wherever `ParameterAutomation`'s existing
block-boundary `checkForNewTargets()` next observes a new generation, per
ADR-004(c)/(d), which is unchanged by this ADR. `rampDurationSampleFrames`
is read, if present, but discarded — not honored, not stored, not
forwarded — exactly matching ADR-007's already-quoted text: "Host-supplied
ramp durations are flattened to target changes under that contract. There
is no claim of sample-accurate host automation or host-ramp fidelity."
This ADR makes no different claim and proposes no ADR-004 revision.

### 5. Publication latency and determinism

**Offline / deterministic path.** The Bridge Controller's drain-and-apply
step is invoked synchronously, once per block, immediately before that
block's `checkForNewTargets()` call, from the same thread driving the
render. Publication latency is therefore identical to what ADR-004(d)
already defines today: a value visible to the Controller before block `N`
takes effect starting in block `N`'s ramp. No additional queuing latency
is introduced. **This is the design's determinism claim**, and it is
scoped precisely: for a fixed, given sequence of producer writes indexed
by the block at which each becomes visible to the Controller, a repeated
offline render that drains synchronously at the same points in the same
order reproduces byte-identical output on every run — the same sense in
which testing.md's PT-5 already establishes reproducibility "per
partition." It is not a claim that a live host session is reproducible
(no real-time system is), only that this project's own deterministic
render/test path remains deterministic under this bridge.

**Online / host path.** Intake happens synchronously on the render thread
(the mailbox write in §3 step 2), so the raw value is known to the system
the instant the host delivers it. The added latency is therefore bounded
by how soon the Bridge Controller's task is next scheduled to drain and
call the setter, publish, and let the *next* render callback's
`checkForNewTargets()` observe the new generation. Concretely, at 48kHz
with a 128-sample host block (~2.67ms) and a Controller woken promptly on
each write (e.g., a semaphore-signaled dedicated thread, or a short fixed
polling period no coarser than one block), publication latency is
expected to be on the order of one to a few block periods (low
single-digit milliseconds) under typical OS scheduling — **this is a
reasoned estimate, not a proven bound**: general-purpose OS thread
scheduling latency for a non-realtime-priority helper task is not
something this ADR can bound numerically. A hard bound, if required,
needs an explicit thread-QoS/priority decision that belongs to the later
bounded implementation plan, not to this design ADR.

### 6. Where the future state-restore producer plugs in

A **StateRestore** producer role is reserved now, structurally identical
to Host and UI: it writes into the same three (one per parameter) mailbox
cells using the same wait-free store-plus-generation primitive, and it
never calls `ParameterAutomation`'s setters directly. This ADR does not
decide: what thread or execution context performs a restore; whether a
restore applies all three parameters atomically as a semantic requirement
(if so, that is new information the later state-schema ADR must supply,
and it would argue for revisiting alternative (C) for that one producer
only — not decided here); versioning; migration; or what a "preset" means
across a changed delay set (already an open item under
ADR-004's Consequences). It decides only the plug point: StateRestore is
one of the three fixed producer roles in §1/§2, using the identical
mailbox mechanism, subject to the same fixed scan-order tie-break in §2.

## Remaining decisions and later evidence

The following are judgment calls this ADR makes for concreteness, not
settled engineering facts, and are called out explicitly for owner
review, in the same spirit as ADR-007's own "Remaining decisions and
later evidence":

- **Cross-producer tie-break order** (§2: proposed StateRestore → Host →
  UI) is a UX decision about what happens when a host automates a
  parameter at the same instant a user touches its on-screen control, or a
  restore lands mid-automation. This ADR picks an order for concreteness;
  it is not derived from any engineering constraint and may be wrong for
  the product.
- **Single-slot last-write-wins coalescing (§2/§3) is a fidelity/
  simplicity tradeoff, not a proof of sufficiency.** Dense host automation
  — many closely spaced events targeting one parameter across several
  render callbacks before the Controller drains — is silently reduced to
  only the last value seen at each drain, which can read as an audible
  "staircase" or a loss of an intended automation shape. This ADR does not
  evaluate that perceptually; like ADR-004's `D_max`, it is a question for
  testing.md's Sonic acceptance gate once real code and audition exist,
  not something a design document can settle by argument.
- **A hard per-callback cap on the number of `AUParameterEvent`s the
  render-thread intake step will scan** (§3) is proposed as defense against
  a pathological or misbehaving host, but no specific cap value, or the
  behavior for events beyond it (silently ignore vs. some other policy),
  is chosen here. This is a robustness/product judgment, not a measured
  requirement.
- **The Bridge Controller's scheduling mechanism and priority** (dedicated
  thread vs. serial dispatch queue; QoS class; wake-on-write vs. polling
  cadence) is deferred entirely to the later bounded implementation plan.
  It is the single biggest lever on the online publication-latency
  estimate in §5, which this ADR could not bound numerically without it.
- **The fixed set of three producer roles (Host, UI, StateRestore) is
  assumed sufficient for the currently-known integration surface.** A
  future producer — e.g., a MIDI-mapped macro, an internal preset-morph
  engine, or a second UI surface (companion app vs. in-host view) — would
  need its own named mailbox set and an explicit decision about its
  position in the tie-break order, not silent reuse of an existing role.
- **Whether a future state-restore requirement genuinely needs
  whole-3-parameter atomicity** is unknown until the later state-schema
  ADR exists. If it does, that ADR should reconsider alternative (C) for
  the StateRestore role specifically, rather than assuming (B) is
  automatically sufficient for every future producer merely because it is
  sufficient for Host and UI today.

## Revisit when

Real device/host evidence contradicts the online scheduling-latency
estimate in §5, or shows the Controller cannot keep the publication
latency low enough for the product's needs; the owner's later
state-restore/state-schema ADR requires whole-parameter-set atomicity for
restore, reopening alternative (C) for that producer role; testing.md's
Sonic acceptance gate finds the single-slot coalescing policy audible as a
defect under realistic host automation density, at which point alternative
(A)'s SPSC ring buffer becomes the named successor, mirroring how
ADR-004(d) named its own rejected (d3) as the successor if block
quantization ever became the dominant time granularity; a fixed
per-callback event cap proves too small (dropped host automation) or
unnecessary (no pathological host observed) once real host traffic is
measured; a fourth producer role becomes necessary; or ADR-004 itself is
revised to add sample-accurate host automation, which would obsolete this
ADR's "flatten to next block boundary" timestamp handling in §4 and
require re-arguing render-thread bounded work under a split-render-loop
design, exactly as ADR-004(d)'s own "Revisit When" already anticipates.
