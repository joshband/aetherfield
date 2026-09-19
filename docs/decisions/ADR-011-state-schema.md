---
id: "ADR-011"
status: accepted
accepted: "2026-09-18"
implementation: "none; design-only, no schema/wrapper/UI/dependency code"
review: "owner-accepted-2026-09-18"
review_document: null
depends_on: [ADR-001, ADR-004, ADR-007, ADR-008]
---

# ADR-011 — Persisted state payload, schema versioning, migration and restore semantics

## Review summary

- **Proposal:** define, at decision level, exactly what Aetherfield
  persists into `AUAudioUnit.fullState`, how that payload is shaped and
  versioned, what happens when a payload is missing, malformed,
  out-of-range or from a newer build, and how a restore is applied through
  the producer slot [ADR-008](ADR-008-parameter-event-bridge.md) already
  reserved for it. The persisted payload is **three normalized `[0,1]`
  doubles (Decay, Damp, Mix), an integer `schemaVersion`, and a
  fixture-identity stamp** recorded for compatibility checking, and
  nothing else. A restore applies all three values as **one atomic
  publish**, per the owner's decision below (§(iii)), not three
  independent setter calls.
- **Why:** [ADR-007](ADR-007-auv3-integration-comparison.md)'s comparison
  table named this as an open prerequisite in its own words —
  "`fullState` already supplies default parameter-tree persistence; custom
  properties may require extension... Define schema/version/migration,
  validation and restore ordering. Neither API choice decides what a preset
  means across fixture changes." ADR-008 then reserved a **StateRestore**
  producer role but explicitly declined to decide what a restore *is*
  (§6), naming this record as the owner of versioning, migration, restore
  context and whole-set atomicity.
- **Consequence:** a bounded wrapper implementation plan can be authorized
  against a concrete, enumerated payload instead of an implicit one; the
  normalized `[0,1]` values are stored raw and are reinterpreted by
  whatever `T60_min`/`T60_max`/`D_max` the loading build derives, which is
  precisely why the fixture stamp is mandatory rather than decorative; and
  two concrete implementation obligations are named, not assumed — **there
  is today no public getter on `ParameterAutomation` for the three
  last-set control values, so nothing outside the class can currently read
  back what to serialize**, and **there is today no way to apply Decay,
  Damp and Mix as one atomic publish — each setter independently calls the
  private `publish()` and bumps the single shared `generation_` counter on
  its own, so three separate setter calls are three separate,
  independently-observable render-thread events, not one.** The owner's
  atomicity decision (below) makes the second obligation load-bearing, not
  optional.
- **Uncertainty:** the hard-reject-vs-per-field-default policy for damaged
  payloads is a product judgment about user trust, not an engineering
  result; whether the identity stamp should also carry the realized delay
  set (not only the specification that derives it) remains open; and
  several implementation-plan-level questions — which component drives the
  pre-render snap sequence, whether the read-back accessor belongs on the
  core or the wrapper, and whether `schemaVersion` should be a plain
  integer or a major/minor pair — are named but not settled here.

**Status: Accepted (2026-09-18).** The owner accepted alternative (i)(B)'s
payload shape (three normalized doubles, an integer `schemaVersion`, and a
fixture-identity stamp extended with `D_max`), alternative (ii)(B)'s
per-field-fallback validation with its two hard-reject carve-outs, and, for
a fixture-stamp mismatch, "accept the normalized values, surface the
mismatch" as the interim policy. **For restore atomicity (alternative
(iii)), the owner selected (B): true atomic 3-parameter application,**
stating that instantaneous, click-free preset switching during live
playback is a real product requirement — the exact new information ADR-008
§6 and its own "Revisit when" named as the trigger for reopening ADR-008's
alternative (C) for the StateRestore role specifically. §4 below records
the resulting design and the new engineering obligation it creates (a
combined-publish entry point on `ParameterAutomation`, named but not
implemented here, in addition to the read-back accessor §1 already names).
Drafting and accepting this record does not authorize adding either
obligation, writing any `NSCoding`/`fullState` serialization, or defining
any on-disk key names or byte format; those are implementation-plan-level
items that require a separately authorized bounded implementation plan
exactly as [ADR-007](ADR-007-auv3-integration-comparison.md) requires and
as [ADR-008](ADR-008-parameter-event-bridge.md),
[ADR-009](ADR-009-production-bus-policy.md) and
[ADR-010](ADR-010-device-lifecycle-matrix.md) each required for their own
subject matter. This record does not otherwise revise ADR-004 or ADR-009,
and it decides nothing about controls that do not exist.

## Context and scope

ADR-007 selected native Apple AUv3 APIs and listed a product-defined state
payload and compatibility policy among the open prerequisites to any
wrapper implementation plan. ADR-008 then built the transport half of the
control path and stopped deliberately short of this one. Its §6 ("Where the
future state-restore producer plugs in") is binding context and is quoted
here in the part that assigns this work:

> This ADR does not decide: what thread or execution context performs a
> restore; whether a restore applies all three parameters atomically as a
> semantic requirement (if so, that is new information the later
> state-schema ADR must supply, and it would argue for revisiting
> alternative (C) for that one producer only — not decided here);
> versioning; migration; or what a "preset" means across a changed delay
> set (already an open item under ADR-004's Consequences). It decides only
> the plug point: StateRestore is one of the three fixed producer roles in
> §1/§2, using the identical mailbox mechanism, subject to the same fixed
> scan-order tie-break in §2.

Those five deferred items — restore context, restore atomicity,
versioning, migration, preset meaning across a changed delay set — are
this ADR's entire scope.

This ADR reopens ADR-008's transport in exactly the one place ADR-008's
own "Revisit when" named as its successor trigger, and nowhere else: the
**StateRestore** role's three single-slot mailbox cells are superseded by
the atomic triple-buffer mechanism §(iii)/§4 decide. The nine-cell design
is now six cells for Host and UI (three parameters × two roles), unchanged
in every respect — mechanism, coalescing, and the Bridge Controller as the
sole permitted caller of `ParameterAutomation::setDecay`/`setDamp`/`setMix`
(for Host/UI) or the new combined entry point (for StateRestore only, §4)
— plus the separate three-slot StateRestore buffer. It does not revise
ADR-004 (c) or (d): the block-boundary target consumption and the fixed
20ms internal ramp remain binding, including for StateRestore. It does not
decide bus layout or bypass behavior — ADR-009 owns those, and its bypass
items are explicitly still open (§5). It does not resurrect any deferred
control.

## What the current code actually does (read this session, not assumed)

Every claim in this section was read directly from
[`src/dsp/ParameterAutomation.h`](../../src/dsp/ParameterAutomation.h),
[`src/dsp/ParameterAutomation.cpp`](../../src/dsp/ParameterAutomation.cpp)
and
[`src/dsp/DiffusionStereoPath.h`](../../src/dsp/DiffusionStereoPath.h)
while drafting this record.

- **The complete public surface of `ParameterAutomation`** is: its
  defaulted default constructor; `prepare(network, sampleRate, dMaxDb)`;
  the three control-thread setters
  `setDecay`/`setDamp`/`setMix(double normalized)`; the render-thread pair
  `checkForNewTargets()` and `advance(network)`; `reset()`; and four
  diagnostic accessors — `nonFiniteRejectionCount()`, `t60Min()`,
  `t60Max()`, `dMax()`. The only other public declaration is the nested
  result type `advance()` returns, `struct MixGains { float dry; float
  wet; }`. There is no other public member function and no public data
  member.
- **The three control values are private and have no getter.**
  `ParameterAutomation` stores `double lastDecay_ = 0.5;`,
  `double lastDamp_ = 0.0;`, `double lastMix_ = 1.0;` as private members.
  The three setters are write-only, and none of the four diagnostic
  accessors returns any of them. `DiffusionStereoPath`'s `setDecay`/
  `setDamp`/`setMix` are documented as "thin forwards" and likewise expose
  no read-back. **Engineering fact, not a guess: as the code stands today,
  no caller outside `ParameterAutomation` can read back what Decay, Damp
  or Mix are currently set to, so there is nothing for a serializer to
  read.** A read-back accessor is therefore a named, required future
  implementation item (§1); this ADR does not add it and does not
  authorize adding it.
- **The setters' validation is the validation a restore can reuse.**
  `setDecay`/`setDamp`/`setMix` each reject a non-finite input — returning
  `false`, incrementing `nonFiniteRejectionCount_`, and leaving the
  published set and generation unchanged — and clamp a finite
  out-of-range input into `[0,1]` via a local `clamp01` before deriving.
  Each also updates its `last*_` member **only after** `publish()` itself
  succeeds, so a derivation that produced a non-finite intermediate leaves
  the stored control value unchanged too.
- **`prepare()`'s defaults are the fallback values a restore falls back
  to.** The header states, and the source confirms, that a successful
  `prepare()` "sets every control to its default (Decay = 0.5, Damp = 0,
  Mix = 1.0 - full wet, matching S2's own fixture convention of no dry
  path), publishes an initial generation, and leaves the object in exactly
  the state `reset()` defines."
- **`reset()` snaps, it does not retarget.** The header: "Snaps every
  smoother to its current target and cancels any in-flight ramp (ADR-004
  (c) point 5)." The source confirms it assigns `current = target` and
  `remaining = 0` for every gain ramp, damping ramp, and the dry/wet
  ramps, and touches nothing else — not the published atomics, not
  `generation_`, not `consumedGeneration_`, not
  `nonFiniteRejectionCount_`, and not the network.
- **The "snap to a restored value" sequence already exists in the code.**
  Because `reset()` snaps each ramp to *that ramp's* `target`, and a
  ramp's `target` is only refreshed by `checkForNewTargets()`, snapping to
  a newly published value requires the ordered triple **publish →
  `checkForNewTargets()` → `reset()`**. `prepare()` performs exactly that
  sequence itself: it publishes the defaults, then calls
  `checkForNewTargets()`, then `reset()`. Calling `reset()` *before*
  `checkForNewTargets()` snaps to the previously published value, not the
  restored one. This ordering is load-bearing for §4 and is an observed
  property of the current code, not a proposal.
- **Only Mix is fixture-independent.** `publish()` derives `T60_0` from
  `t60Min_`/`t60Max_` (computed at `prepare()` from the network's own
  `m_min`/`m_max` and the sample rate), derives the damping excess as
  `excessDb = dMax_ * damp`, and derives dry/wet as an equal-power
  `cos`/`sin` pair of Mix alone with both endpoints assigned exactly.
  Consequently a stored normalized Decay maps to different seconds under a
  different `N`, delay set or sample rate, **and a stored normalized Damp
  maps to a different dB of excess damping under a different `D_max`** —
  and `ParameterAutomation.h` states plainly that `dMaxDb` is today "this
  plan's test-fixture value (48.0) or another explicitly labeled
  non-product value; never a value presented as a product decision."
  Mix alone carries no fixture dependence.
- **There is no bypass flag, and no other persistable control, anywhere in
  the core.** `DiffusionStereoPath`'s public surface is `prepare`,
  `reset`, `isPrepared`, the three delay-sample accessors,
  `allpassCoefficient()`, `preStepTapSums()`, the three control forwards,
  `processSample`, `process`, and the three fault accessors
  `nonFiniteCount()`/`nonFiniteLatched()`/`resetPending()` — plus its
  constructor and destructor, its deleted copy and declared move
  operations, and one `AETHERFIELD_TESTING`-guarded test seam
  (`setFdnNonFiniteStateForTest`), none of which is a control. No bypass
  member exists; consistently,
  [ADR-009](ADR-009-production-bus-policy.md)'s own "Remaining decisions"
  still lists the "Bypass CPU-vs-tail-continuity tradeoff" and whether a
  host-exposed "kill the wet tail instantly on bypass" affordance should
  exist as unresolved. This ADR therefore persists no bypass state and
  does not assume one exists.
- **`DiffusionStereoConfig` is preparation input, not user state.** Its
  members (`sampleRate`, `lineCount`, `fdnMinDelaySeconds`,
  `fdnMaxDelaySeconds`, `t60ZeroSeconds`, `t60PiSeconds`, `dMaxDb`, the
  input/left/right delay-time arrays, `allpassCoefficient`) are a fixed
  build-time fixture specification — the header calls the allpass
  coefficient "validation data rather than a runtime control." They are
  not things a user changes, so they are not persisted *as state*; a
  subset is persisted as an **identity stamp** (§2).

### Persistent controls vs. transient DSP history

This distinction is the spine of the whole record.

**Persistent controls (persisted):** the three normalized `[0,1]` control
values. They are the only quantities a user sets, the only ones a preset
means, and the only ones whose loss is user-visible as "my settings were
forgotten."

**Transient DSP history (never persisted, never restored):** delay-line
contents, the FDN's internal line state, allpass state, the per-coefficient
ramp state (`current`/`target`/`increment`/`remaining`) and
`consumedGeneration_`, and the fault-detector state
(`nonFiniteCount`/`nonFiniteLatched`/`resetPending`). None of it is
serialized.

Restoring a control value and snapshotting a reverb's internal buffers are
different operations with different risks, and this ADR chooses the first
only. Restoring parameter *targets* and letting the DSP re-settle is
governed entirely by mechanisms ADR-004 already accepted and the code
already implements: ADR-004 (c)'s fixed 20ms ramp carries the engine
smoothly to a restored value during a live restore, and `reset()`'s
documented snap behavior — combined with the publish →
`checkForNewTargets()` → `reset()` ordering above — makes a restore at
instantiation start *at* the restored value with no audible slew from the
defaults. Attempting instead to snapshot and replay internal buffer
contents would re-open ADR-003's numerical-safety argument for a state the
core never produced itself, would tie the persisted format to the delay
set at byte level (making ADR-005's already-live migration obligation
vastly worse), and buys only tail continuity across a session boundary
that no requirement in this repository asks for. It is rejected outright
and is not offered as an alternative below.

## Alternatives

### (i) Payload shape and versioning

| Criterion | (A) Rely on `fullState`'s default parameter-tree persistence alone, no custom payload | (B) Flat versioned dictionary: `schemaVersion` (int) + three normalized doubles + fixture-identity stamp — **recommended** | (C) Nested/self-describing payload: per-parameter records carrying identifier, normalized value, and the derived engineering value (seconds, dB) |
|---|---|---|---|
| What is actually stored | Whatever the AUParameterTree holds, in Apple's own representation, with no version field of this project's own | **Three raw normalized `[0,1]` doubles — the exact quantity ADR-004 made canonical — plus an explicit integer version and the fixture stamp ADR-005 already requires of any recorded preset** | The same three values plus derived seconds/dB that are recomputable from them, so the payload carries redundant data that can disagree with itself |
| Version/migration story | None this project controls: a field added later is indistinguishable from a field that was never there, and there is no place to put a migration discriminator | **Explicit: `schemaVersion` is the single discriminator every migration branches on; an added field is a version bump, a removed field is ignored by newer readers, a renamed field is a version bump with an explicit mapping** | Explicit too, but the redundancy creates a second migration question — which representation wins when the stored derived value and the recomputed one disagree after a fixture change |
| Handles ADR-005's fixture-identity requirement | No — nothing records `N`, `t_min`, `t_max`, `f_s`, so a preset saved under one delay set is silently reinterpreted under another | **Yes, directly: the stamp is the mechanism for the compatibility check in §3** | Yes, but by storing derived values rather than the identity that produced them, which answers "what did it sound like" less reliably than storing the identity |
| Complexity / surface area | Lowest, but the complexity reappears later as an un-versioned format that cannot be migrated | **Low: a fixed, small, flat set of scalars; no nesting, no collections, no ordering dependence** | Highest: more fields, more invariants, more ways to be internally inconsistent |
| Fits the already-accepted canonical quantity | Indirectly — the parameter tree's values are normalized, but the project has no contract over Apple's representation | **Exactly: ADR-004 made the normalized `[0,1]` value canonical, and ADR-005 recorded that presets must carry `N`, `t_min`, `t_max`, `f_s` alongside the numbers** | Over-specifies: stores quantities ADR-004 treats as *derived*, not canonical |
| Forward compatibility | Undefined behavior when a future build adds product state | **Defined by §3's policy on an unrecognized higher version** | Same as (B), with more fields to reconcile |

**Decision: (B) (owner-accepted, 2026-09-18).** It stores exactly the canonical quantity ADR-004
already defined and nothing that can be recomputed from it; it gives
migration a single, explicit discriminator instead of inference; and its
fixture stamp is not an invention of this ADR but the direct discharge of
an obligation ADR-004 recorded and ADR-005 made live. (A) is rejected
because "no version field" is not a versioning policy, and ADR-007
explicitly asked this project to "define schema/version/migration" rather
than inherit whatever the parameter tree happens to encode — note that (B)
does not forbid *also* letting `fullState` carry the parameter tree, it
requires that this project's own versioned payload is the authoritative
one on restore. (C) is rejected because storing a derived value alongside
the canonical one creates a disagreement case with no principled winner,
which is exactly the class of problem a fixture change will trigger.

**Proposed payload fields (names illustrative; concrete key spellings are
implementation-plan-level and explicitly not decided here):**

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | integer, starts at 1 | The single migration discriminator. Bumped on any add, remove, rename or semantic change. |
| `decayNormalized` | double in `[0,1]` | ADR-004's canonical Decay value. |
| `dampNormalized` | double in `[0,1]` | ADR-004's canonical Damp value. |
| `mixNormalized` | double in `[0,1]` | ADR-004's canonical Mix value. |
| `fixtureLineCount` (`N`) | integer | Identity stamp — ADR-005: presets "must record `N`, `t_min`, `t_max` and `f_s` alongside the numbers." |
| `fixtureMinDelaySeconds` (`t_min`) | double | Identity stamp. |
| `fixtureMaxDelaySeconds` (`t_max`) | double | Identity stamp. |
| `fixtureSampleRate` (`f_s`) | double | Identity stamp; `T60_min`/`T60_max` are rate-dependent. |
| `fixtureDMaxDb` (`D_max`) | double | Identity stamp. **Not named by ADR-005**; added here (owner-accepted 2026-09-18) on the grounded basis that `publish()` derives `excessDb = dMax_ * damp`, so a changed `D_max` changes what a stored Damp means exactly as a changed delay set changes what a stored Decay means. |

**A caveat on what the stamp can and cannot detect.** The stamp records
the fixture *specification* ADR-005 names (`N`, `t_min`, `t_max`, `f_s`),
not the realized integer delay set `mᵢ`, and `publish()` derives
`T60_min`/`T60_max` from the network's own `m_min`/`m_max`. Those agree
only while the specification-to-`mᵢ` derivation rule is itself fixed, so a
future change to that rule (ADR-005's nearest-prime selection with its
tie-break and monotonicity guard) at an unchanged `(N, t_min, t_max,
f_s)` would change what a stored Decay means without changing the stamp.
Whether the stamp should therefore also carry the realized
`m_min`/`m_max` (or a hash of the full `mᵢ`) is named in "Remaining
decisions" rather than decided here.

**Migration rule.** A reader handles a payload whose `schemaVersion` is
*lower* than its own by applying an explicit, ordered chain of
version-to-version transforms, each of which must supply a defined value
for any field the newer version added; a field a newer version removed is
read and discarded. A *renamed* field is a version bump with an explicit
old-name-to-new-name mapping, never a silent reuse of a key. **A change to
`N` or the delay set is not a schema-version event and is not fixed by a
version bump** — it is the separate compatibility question §3 handles via
the fixture stamp, because ADR-004's Consequences already settled that "an
automation curve or a stored preset is only comparable within a fixed
delay set: a later change to `N` or the delay lengths is a
state-migration obligation, not a free change," and ADR-005 confirmed that
obligation "is now live rather than hypothetical."

### (ii) Handling an invalid, missing or incompatible payload

| Criterion | (A) Hard reject: any defect discards the whole payload and the unit stays at `prepare()`'s defaults | (B) Per-field fallback: each field is validated independently; a defective field falls back to its `prepare()` default, valid fields are applied — **recommended, with the two hard-reject carve-outs below** | (C) Best-effort salvage: accept anything parseable, coerce silently, never report |
|---|---|---|---|
| User-visible behavior on a single corrupt field | Everything is lost, including two perfectly good values | **Two settings survive, one returns to default — the smallest correct loss** | Two settings survive and the third takes an arbitrary coerced value that may be neither the stored nor the default one |
| Reuses an existing validated mechanism | Yes, trivially (nothing is applied) | **Yes, and precisely: routing every restored value through `setDecay`/`setDamp`/`setMix` reuses their already-tested contract — non-finite rejected with the published set unchanged and `nonFiniteRejectionCount_` incremented, finite out-of-range clamped to `[0,1]`** | No — silent coercion is a new, untested behavior |
| Risk of a wrong-sounding restore | Lowest | Present but bounded: a fallback field is a *documented default*, not an arbitrary value | Highest: a coerced value has no defined meaning |
| Diagnosability | Good (one clear outcome) | **Good: `nonFiniteRejectionCount()` already counts non-finite rejections, so a restore that hit them is observable through an accessor that exists today** | Poor by construction |
| Behavior on an unrecognized *higher* `schemaVersion` | Safe | Unsafe if applied naively — a newer version may have changed what a field *means*, not just added fields | Unsafe |

**Decision: (B) (owner-accepted, 2026-09-18), with two explicit carve-outs where hard reject
wins.**

1. **Per-field fallback is the default policy.** Each of the three values
   is validated independently and, if absent or non-finite, replaced by
   its `prepare()` default (Decay `0.5`, Damp `0.0`, Mix `1.0`) — the
   values the header already documents. A finite out-of-range value is
   *clamped, not rejected*, because that is already the setters'
   documented contract and restore should not invent a second, different
   rule. The recommended implementation is to route every restored value
   through the same `setDecay`/`setDamp`/`setMix` path (via ADR-008's
   StateRestore mailbox, §4) rather than write a parallel validator, so
   there is exactly one place in the system that decides what a valid
   control value is.
2. **Carve-out A — missing or unrecognized `schemaVersion`: reject the
   whole payload and stay at defaults.** Without a version there is no
   basis for interpreting any field, and a *higher* version than the
   reader understands may have redefined a field's meaning rather than
   merely added one. Silently applying such a payload risks restoring a
   user's session to values that mean something different from what they
   saved. Refusing is the conservative outcome, and the cost — the unit
   opens at defaults — is the same cost a first-ever launch already pays.
3. **Carve-out B — a fixture-stamp mismatch is a distinct, owner-gated
   case, and this ADR does not settle it.** See "Remaining decisions": the
   three candidate behaviors are refuse-and-default, accept-the-normalized-
   values-anyway (they remain in range, only their *meaning* in seconds/dB
   shifts), or attempt a value remap. A remap is not currently computable
   for a general fixture change, and the product's `N` is still open by
   design (ADR-005), so this ADR proposes **accept the normalized values
   and surface the mismatch** as the least-destructive interim behavior
   while flagging it explicitly as the owner's call.

An entirely absent payload (a first launch, or a host that saved nothing)
is not a defect: it is the defaults path, and no error is reported.

### (iii) Restore application: three sequential mailbox writes vs. true atomic application

This is the question ADR-008 §6 handed here, and ADR-008's alternatives
table already anticipated it: whole-set atomicity "would only need it if a
state-restore producer must apply an atomic 3-parameter preset; that
requirement, if real, belongs to the later state-schema ADR, not to this
transport choice."

| Criterion | (A) Three back-to-back StateRestore mailbox writes, drained by the existing Bridge Controller | (B) True atomic 3-parameter application: reopen ADR-008 alternative (C) (a fixed triple-buffer of the full tuple) for the StateRestore role only — **decided (owner-accepted, 2026-09-18)** |
|---|---|---|
| Change to ADR-008 | **None.** Uses the reserved StateRestore role exactly as specified: three wait-free writes into the three StateRestore cells, drained in the Controller's fixed scan order. | Reopens an accepted ADR's transport for one producer, adding a second mechanism alongside the mailboxes. |
| Worst case if the drain lands mid-write | The Controller observes some values from the restore and some from before it, for **at most one drain interval**, after which all three are correct. Each parameter still ramps over ADR-004 (c)'s 20ms. | By construction, all three arrive together or none do. |
| Is that worst case audible? | A sub-drain-interval window in which, say, Decay has moved but Mix has not. Both are individually valid, in-range, ADR-003-safe values; the combination is a momentary blend of old and new, not an undefined state — the same relaxation ADR-003 (c) already accepted and ADR-004 (d) already relies on for the coefficient set itself. | Not applicable. |
| Restore-at-instantiation (the dominant case) | **Not affected at all:** no audio has been rendered yet, so there is no combination to be heard. The publish → `checkForNewTargets()` → `reset()` snap (§4) makes the first rendered sample already correct. | Same. |
| Live restore mid-playback (host preset switch) | The only case where the window exists at all, and only if the product intends instantaneous preset switching during playback. | The only case that motivates (B). |
| Cost | **Zero new mechanism; nine cells stay nine cells.** | A second transport shape, its own buffer-lifetime reasoning — the class of complexity ADR-004 (d) rejected its own (d2) for, and ADR-008 rejected its (C) for. |
| Interaction with ADR-008's tie-break | Inherits it unchanged. | Needs a new rule for how an atomic tuple loses or wins against per-parameter Host/UI cells, which ADR-008 did not design. |

**Decision: (B), true atomic 3-parameter application (owner-accepted,
2026-09-18).** The owner stated that instantaneous, click-free preset
switching *during live playback* is a real product requirement, not a
hypothetical — precisely the new information ADR-008 §6 and its own
"Revisit when" named as the trigger for reopening ADR-008's alternative
(C) for the StateRestore role specifically. This does not reopen or change
ADR-008's mailbox transport for the Host or UI roles, which are unaffected
and unchanged; it adds a second, separate mechanism used only by
StateRestore.

**Why (C)'s original objection does not transfer to StateRestore alone.**
ADR-008 rejected (C) as a *general* producer→controller mechanism because
"O(1) writer must first snapshot the *other two* parameters' current
values into the same buffer slot, or lose them — extra state the producer
must track." That objection is specific to Host and UI, which touch one
parameter at a time and would have to fabricate values for the other two
on every write. **A restore never has this problem**: a preset is by
definition a complete triple, so the producer always has all three values
in hand and there is no snapshot-or-lose-them burden to invent. What was a
real cost for Host/UI is a non-issue for StateRestore.

**The concrete mechanism.** A fixed pool of **three** preallocated tuple
buffers, `std::array<{double decay; double damp; double mix;}, 3>
restoreTuples_`, dedicated to the StateRestore role only (separate from
the nine Host/UI/StateRestore mailbox cells, which continue to exist for
Host and UI unchanged; StateRestore's three mailbox cells are superseded
by this mechanism and no longer used). A restore write: computes the next
slot as `(lastPublishedIndex + 1) % 3` (single-writer, so no contention on
this arithmetic), writes the complete tuple into that slot, then
release-publishes `{slotIndex, generation}` — one `std::atomic<uint32_t>`
slot index and one `std::atomic<uint64_t>` generation, following the exact
acquire/release shape `ParameterAutomation`'s own transport already uses.
Three slots (not two) give the Controller a full spare slot of margin
against back-to-back restores before a drain, mirroring why alternative
(C) was itself named a "triple-buffer" in ADR-008's own table. The Bridge
Controller's drain: acquire-load the generation; if changed since last
consumed, acquire-load the slot index, read the tuple from that slot, and
apply it as one call to the new combined-publish entry point below — never
as three separate setter calls, which would defeat the atomicity this
decision exists to provide.

**The obligation this creates, named and not implemented here.** Today,
`ParameterAutomation::setDecay`/`setDamp`/`setMix` are the *only* public
way to change a control, and each independently calls the private
`publish(lineCount, decay, damp, mix)` and bumps the single shared
`generation_` counter on its own — confirmed this session by reading
`ParameterAutomation.cpp`: `setDecay` calls
`publish(lineCount_, clamped, lastDamp_, lastMix_)`, `setDamp` calls
`publish(lineCount_, lastDecay_, clamped, lastMix_)`, and `setMix`
symmetrically, each as its own call. Three such calls from the Controller,
even issued back-to-back with nothing else interleaved, are **three
separate `generation_` increments**, and the render thread's
`checkForNewTargets()` runs independently on its own schedule — it could
observe the first increment and start ramping Decay toward its new target
before the Controller has even called `setDamp`, which is exactly the torn
application (B) exists to prevent. **True atomicity therefore requires a
new public entry point** — the minimal shape being
`ParameterAutomation::setAll(double decay, double damp, double mix)
noexcept`, which validates all three inputs, calls the *existing* private
`publish(lineCount_, decay, damp, mix)` **exactly once**, and updates all
three `last*_` members together on success — reusing the identical
derivation and transport `publish()` already implements, adding no new
math and no new threading contract. This ADR names this obligation
precisely as it named the read-back accessor in §1: **required by this
decision, not authorized by it.**

## Decision and tradeoffs

### 1. What "state" is

**Persisted:** the three normalized `[0,1]` control values (Decay, Damp,
Mix), an integer `schemaVersion`, and the fixture-identity stamp of the
build that saved them (§2's table).

**Not persisted:** everything else. Specifically and deliberately: no
delay-line or FDN internal state, no allpass state, no ramp state, no
`consumedGeneration_`, no fault-detector state, no `DiffusionStereoConfig`
member as a *setting* (only the subset reproduced as an identity stamp),
and no bypass flag — because no bypass flag exists in the core, and
ADR-009 has not decided whether one will (see "What this ADR does not
decide").

**Named implementation obligation (engineering fact, verified this
session):** `ParameterAutomation` has no public read-back for
`lastDecay_`/`lastDamp_`/`lastMix_`, and neither does
`DiffusionStereoPath`. A save path therefore cannot be written against the
current public surface. A future implementation plan must add a read-back
accessor — the minimal shape being three `const noexcept` getters
alongside the existing diagnostics, or one small aggregate returning all
three — and it must be a plain read of the already-stored
control-thread values, introducing no new derivation and no new threading
contract. **This ADR names that obligation and does not authorize it.** A
wrapper that instead caches the last value it *sent* is a plausible
alternative that avoids the core change entirely, and is listed in
Remaining Decisions rather than chosen here, because it makes the wrapper
rather than the engine the source of truth. §4 names a second, related
obligation on the restore-application side (`setAll`, for atomic 3-parameter
publish) — the two are independent additions to the same class.

### 2. Payload shape and versioning

Per alternative (i)(B): a flat dictionary of scalars — `schemaVersion`,
three normalized doubles, and the five-field fixture stamp. The stamp
discharges ADR-004's Consequences and ADR-005's explicit requirement that
"S2 results and any presets taken from them must record `N`, `t_min`,
`t_max` and `f_s` alongside the numbers," and extends it with `D_max` on
the derivation grounds in "What the current code actually does."

Migration is a version-indexed chain of explicit transforms; a delay-set
or `D_max` change is *not* a version event but a stamp-mismatch event
(§3). `schemaVersion` starts at 1 and is bumped for any add, remove,
rename or semantic change, never reused.

### 3. Invalid, missing and incompatible payloads

Per alternative (ii) (owner-accepted): per-field fallback to `prepare()`'s
documented defaults (Decay `0.5`, Damp `0.0`, Mix `1.0`) for an absent or
non-finite field; clamping for a finite out-of-range field, reusing the
setters' existing validation logic rather than inventing a second rule;
whole-payload rejection (defaults, no partial apply) when `schemaVersion`
is missing or higher than the reader understands; and, for a fixture-stamp
mismatch, the owner-accepted policy of accepting the normalized values
while surfacing the mismatch.

Validation happens off the render thread, before the atomic publish in
§4: the restore path checks each field (finiteness, range, `schemaVersion`,
fixture stamp) and resolves fallbacks there, so `setAll` (§4) receives
three already-valid, already-clamped values and its own internal
finiteness check — mirroring `setDecay`/`setDamp`/`setMix`'s existing
`nonFiniteRejectionCount()`-incrementing behavior — is a defense-in-depth
backstop, not the primary validation path. This keeps validation logic in
one place conceptually (the same rules the individual setters already
enforce) even though it is invoked from the restore path rather than from
three separate setter calls.

### 4. Restore ordering and its interaction with ADR-008's bridge

A restore is a **StateRestore**-role producer exactly as ADR-008 §6
reserved. Concretely:

1. **Execution context.** A restore runs on a non-render, non-reentrant
   control context — never the render thread, and never by calling
   `ParameterAutomation`'s setters or the new `setAll` entry point
   directly from anywhere but the Bridge Controller, which ADR-008 §1
   makes a hard invariant and this ADR extends to `setAll`. Deserialization,
   validation and the fixture-stamp check all happen there, off the render
   thread, before anything is written. The restore path writes the
   complete tuple into the next StateRestore buffer slot and performs
   **one** wait-free publish (slot index, then a release-ordered
   generation increment) and returns; the Bridge Controller does the
   derivation — a single `setAll` call — as it does a setter call for
   every other producer.
2. **Ordering against other producers generalizes ADR-008 §2's scan order;
   it does not discard it.** ADR-008 §2's fixed order was Host, then
   StateRestore, then UI, with the consequence that "whichever role is
   scanned last in a given pass wins." Because StateRestore no longer
   shares a per-parameter mailbox cell with Host and UI, "scanned" is
   redefined as **applied**: within one drain, the Controller applies any
   pending Host mailbox values first (`setDecay`/`setDamp`/`setMix` for
   whichever parameters changed), then a pending StateRestore tuple if one
   is ready (`setAll`, exactly once), then any pending UI mailbox values
   last. This reproduces the original intent exactly: a restore still
   overrides a *stale* pending Host value from an earlier pass (Host is
   applied before StateRestore), and a same-pass UI touch still wins over
   a restore (UI is applied after StateRestore) — an outcome ADR-008
   named as accepted rather than guaranteed, and unchanged by this ADR.
   Applying `setAll` between the Host and UI mailbox passes, rather than
   folding it into either, keeps the atomic write atomic: no per-parameter
   Host or UI call is interleaved inside it.
3. **Atomicity.** True atomic application, per alternative (iii)(B): the
   Controller's `setAll` call derives and publishes all three coefficient
   sets from **one** call to the existing private `publish()`, with **one**
   `generation_` increment covering all three — never split across two
   drains, and never observable by `checkForNewTargets()` as a partial
   triple. Each parameter still ramps over ADR-004 (c)'s 20ms from
   whatever it held before, exactly as any other retargeting does; ramping
   is unaffected by atomicity, which concerns only whether the *targets*
   land together.
4. **Restore at instantiation should snap, not ramp.** When the restore
   happens before any audio has been rendered — the dominant case — the
   engine should begin *at* the restored values rather than sliding to
   them from the defaults. The existing mechanism for that is the ordered
   triple observed in the source: publish, then `checkForNewTargets()`,
   then `reset()`. `prepare()` already performs exactly this sequence for
   its own defaults; with `setAll`, "publish" here is the single combined
   call, not up to three separate ones, which makes the snap itself atomic
   too — there is no window in which one coefficient has snapped and
   another has not. This ADR decides the *semantics* (a pre-render
   restore snaps; a restore during playback ramps under ADR-004 (c)) and
   leaves to the implementation plan the question of which component
   drives that sequence, given that `checkForNewTargets()` and `reset()`
   are documented as render-thread API while the Bridge Controller is not
   the render thread. That sequencing question is named in Remaining
   Decisions rather than answered here.
5. **A restore never clears the fault counters.** `reset()` already
   preserves `nonFiniteRejectionCount_`, and `DiffusionStereoPath`
   documents that "the aggregate counter is intentionally preserved";
   restoring a preset is not a diagnostic reset and must not behave as
   one.

### 5. What this ADR does not decide

- **No implementation of anything.** No wrapper, no UI, no serialization
  code, no dependency, and not the `ParameterAutomation` getter §1 names.
- **No concrete `fullState` encoding.** No `NSCoding` conformance, no
  dictionary key spellings, no on-disk byte layout, no plist-vs-archiver
  choice, no decision about `fullStateForDocument` as distinct from
  `fullState`. All implementation-plan-level.
- **No non-parameter product state.** Size, Pre-delay, Diffusion, Width,
  Low Damp, Freeze, Bloom, Texture, Mod Depth and Mod Rate are all
  deferred by ADR-004 with named blockers and are not resurrected,
  reserved for, or hinted at in this schema. If one is ever accepted, it
  is a `schemaVersion` bump with an explicit migration, which is precisely
  what §2's versioning exists to make possible.
- **No bypass-flag persistence.** ADR-009 has not decided whether a
  bypass affordance exists, what it does to the tail, or whether a
  "kill tail on bypass" control should be host-exposed. This ADR persists
  no bypass state and takes no position on whether one should exist.
- **No preset *browser*, factory-preset library, `factoryPresets`/
  `currentPreset` behavior, or preset file interchange format.** This ADR
  defines what a state payload contains, not a preset-management feature.
- **No revision of ADR-008's transport, ADR-004 (c)/(d), or ADR-009's bus
  policy.**

## Remaining decisions and later evidence

The following are judgment calls this ADR makes for concreteness, not
settled engineering facts, and are called out explicitly for owner review,
in the same spirit as ADR-007/ADR-008's own "Remaining decisions and later
evidence":

- **Decided: restore requires atomic 3-parameter application** (alternative
  (iii)(B), owner-accepted 2026-09-18). The owner confirmed instantaneous,
  click-free preset switching *during playback* as a real product
  requirement, making ADR-008's alternative (C) live for the StateRestore
  role, exactly as ADR-008 §6 and its "Revisit when" anticipated. This
  creates the `ParameterAutomation::setAll` obligation named in §4 —
  **named, not implemented, by this record.** Left open beneath this
  decision: whether `setAll`'s validation should reject the whole triple
  on any single non-finite/out-of-range field or apply per-field
  clamping/fallback identically to the individual setters (this record's
  §(ii) policy assumes the latter but does not spell out `setAll`'s exact
  contract, which is an implementation-plan-level detail).
- **Decided: the fixture-stamp mismatch policy is "accept the normalized
  values, surface the mismatch"** (§3 carve-out B, owner-accepted
  2026-09-18). This means a user's saved preset can sound materially
  different after a delay-set change while still "loading successfully" —
  an accepted consequence, not an oversight. The concrete surfacing
  mechanism (a UI indicator, a log line, a host-visible property) is an
  implementation-plan-level detail this record does not specify.
- **Decided: `D_max` is added to the identity stamp** (owner-accepted
  2026-09-18), on the grounded derivation basis that `publish()` derives
  `excessDb = dMax_ * damp`, so a changed `D_max` changes what a stored
  Damp means exactly as a changed delay set changes what a stored Decay
  means. ADR-005's own list named only `N`, `t_min`, `t_max`, `f_s`; this
  record extends it.
- **Whether the stamp should also record the realized delay set**, not
  only the specification that produces it (§2's caveat). `publish()`
  derives `T60_min`/`T60_max` from the network's own `m_min`/`m_max`, so a
  later change to ADR-005's nearest-prime `mᵢ` derivation rule at an
  unchanged `(N, t_min, t_max, f_s)` would change what a stored Decay
  means while leaving the stamp identical. Adding `m_min`/`m_max` (or a
  hash of the full `mᵢ`) would close that hole, at the cost of a field
  ADR-005 does not name.
- **Whether the read-back accessor belongs on `ParameterAutomation` at
  all** (§1), or whether the wrapper should instead cache the last value
  it sent and serialize that. The former keeps the engine the single
  source of truth and survives any future producer writing to the engine;
  the latter avoids touching `src/dsp/` at all, which ADR-001's portable-
  core boundary makes attractive. This ADR recommends the accessor but
  does not treat the question as closed.
- **Which component drives the publish → `checkForNewTargets()` →
  `reset()` snap sequence for a pre-render restore** (§4 item 4), given
  that the latter two are documented render-thread API and the Bridge
  Controller is by definition not the render thread. An equally valid
  resolution is that no explicit snap is needed because the first render
  callback's own `checkForNewTargets()` plus a 20ms ramp from the defaults
  is inaudible under a host's typical pre-roll — a perceptual claim this
  ADR cannot verify and does not assert.
- **Hard-reject vs. per-field fallback** (alternative (ii)) is a product
  judgment about which failure is less surprising to a user, not a
  derived result. The recommendation of per-field fallback assumes a user
  prefers keeping two of three settings over losing all three; the
  opposite preference is defensible.
- **Whether `schemaVersion` should be a plain integer or a
  major/minor pair**, allowing additive-only changes to be forward-
  compatible rather than rejected. This ADR chooses the simpler single
  integer plus a hard reject on an unrecognized higher value; a
  major/minor split would let a newer additive payload load in an older
  build, at the cost of a more complex compatibility rule.
- **Whether a restore should be observable to the UI as a distinct event**
  (so a UI can refresh its controls) rather than as three ordinary
  parameter changes. ADR-008's producer model has no notification channel
  in either direction; adding one is a design change to that ADR, not a
  detail of this one.

## Revisit when

The implementation plan defines `setAll`'s exact validation contract (whole-
triple reject vs. per-field fallback), which this record names as an
implementation-plan-level detail rather than settling; a fourth
persistable control is accepted (Size is ADR-004's named leading
candidate), which is a `schemaVersion` bump and the first real exercise of
§2's migration chain; a bypass affordance is decided under ADR-009, which
would make bypass state a candidate for persistence and reopen §1's
enumeration; the product's `N`, delay set or `D_max` is decided, which
turns ADR-005's migration obligation from live-but-hypothetical into a
concrete one-time migration and forces the fixture-mismatch policy in §3
from an accepted interim behavior into a directly-tested one; ADR-004
(c)'s 20ms ramp or `reset()`'s snap semantics are revised, which would
change what "restore then re-settle" means in §4; ADR-008's fixed scan
order or Host/UI mailbox transport is revised, which §4's ordering
generalization inherits; a real host is observed calling `fullState`
setters at a time or on a thread this design did not anticipate, which is
the first genuine device evidence this record could be wrong about §4
item 1 — and, per ADR-010, no iOS device or simulator has ever run any
Aetherfield code, so none of this has been tested against a real host; the
implementation plan finds the three-slot StateRestore buffer pool
insufficient under real restore rates (e.g., rapid preset-browsing before
a drain completes), which would require enlarging the pool or adding
back-pressure, neither designed here; or a preset-browser/factory-preset
feature is proposed, which this ADR explicitly does not cover and which
would need its own record.
