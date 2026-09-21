# Versioned state and atomic restore — scoping/task plan

**STATUS: SCOPING / PLAN ONLY (2026-09-20). No implementation, test
execution, ADR amendment, or host/device execution is authorized by this
document. A separate owner authorization and the prerequisite decisions
below are required before any implementation checkpoint starts.**

**Goal:** bound the remaining ADR-011 work: a versioned three-control
payload, validated restore, and whole-triple application through the
Bridge Controller, with pre-render snap and live 20ms retargeting.

**Architecture:** retain the portable C++ DSP and wrapper separation from
the native AUv3 glue. State parsing and serialization stay off rendering;
the Bridge Controller remains the serialized owner of normalized targets.
The existing read-back accessor is a prerequisite already present, not new
work or evidence that state restore exists.

**Sources:** [ADR-011](../decisions/ADR-011-state-schema.md), especially
Decision §§1–4, its 2026-09-19 Design note, and Remaining decisions;
[ADR-008](../decisions/ADR-008-parameter-event-bridge.md) §§1–7 and its
StateRestore amendment; [wrapper skeleton plan](phase1-wrapper-skeleton-plan.md)
Non-goals and Tasks 2–3; [roadmap](../roadmap.md); and
[ADR-012 HT-7](../decisions/ADR-012-host-device-acceptance-catalog.md#ht-7--state-saverecall-fullstate-round-trip).
Read those selected sections before implementing. Existing phase-plan
checkpoint conventions govern the future work; this scoping document does
not supply implementation code or resolve the blocked architecture below.

## Current source baseline and scope boundary

Read-only source inspection at `145a69f` establishes:

| Surface | Present behavior | Remaining scope |
|---|---|---|
| `ParameterAutomation::getAll()` | Implemented; returns `NormalizedControls { double decay; double damp; double mix; }` from the three stored last-accepted targets | Reuse on the serialized control context only; no new accessor implementation |
| `DiffusionStereoPath::controls()` | Implemented forwarding surface for the owned automation | Use for an accepted-target snapshot; not a render-thread reader |
| `ParameterAutomation::setAll()` | Absent | Combined validation, derivation and publication obligation |
| `ParameterBridge` | Six Host/UI mailbox cells; a per-parameter winner is applied once per drain | Separate StateRestore transport and Host → StateRestore → UI application order |
| `AetherfieldAudioUnit` | Serial controller queue/timer, native parameter tree, lifecycle and hybrid bypass glue | No project `fullState` override or versioned state codec; no StateRestore producer |
| Current target transport | Independent atomic coefficient fields, then a generation increment; reader loads generation then fields | Must establish coherence under concurrent publication before claiming atomic targets |

The wrapper's `_lastDecay`/`_lastDamp`/`_lastMix` atomics answer parameter
tree reads; they are not the serialization authority. Saves must use
accepted engine targets, not values merely sent to a mailbox.

Some historical status prose in ADR-011, the roadmap and wrapper plan
still says the getter is unimplemented, and some says hybrid bypass is
unimplemented. Those are stale implementation snapshots, not reasons to
reimplement existing work. This plan records the discrepancy without
editing those records. No build, test, sanitizer, render, or host evidence
was produced while drafting it.

## Non-goals

- Product controls beyond Decay, Damp and Mix; final fixture decisions;
  changes to DSP mappings, the 20ms ramp, or block-boundary automation.
- Persistence of bypass, transient delay/allpass/FDN history, ramp state,
  fault state, or the whole preparation configuration as user settings.
- UI, a preset browser, factory presets, preset file interchange, a new
  notification channel, signing/distribution, or deployment-policy changes.
- General Host/UI mailbox redesign, sample-accurate host automation, bypass
  redesign, or a claim to close HT-1…HT-12.
- Implementing the literal ADR-011 triple-buffer sketch without resolving
  its ownership proof. A design correction is not implicitly authorized.

## Accepted behavior to preserve

The payload is a flat versioned set of scalars: integer `schemaVersion`
starting at 1; three raw normalized doubles; and the fixture stamp
`N`, `t_min`, `t_max`, `f_s`, `D_max`. The loading build derives engineering
values using its own fixture. Field labels in ADR-011 are illustrative;
final key spellings and the AU dictionary envelope are decisions still to
record before implementing a codec.

| Input / operation | Required behavior |
|---|---|
| Entire payload absent | Defaults `(0.5, 0.0, 1.0)`; ordinary first-launch path, no error |
| A control absent or non-finite | Substitute that control's documented default before publishing a complete tuple |
| A finite control outside `[0,1]` | Clamp independently to `[0,1]` |
| Missing or unrecognized version, including a newer version | Reject the entire payload; no salvage of apparently familiar fields; ADR-011 specifies defaults |
| Recognized fixture mismatch | Accept normalized values and surface the mismatch; do not remap seconds/dB |
| Valid live restore | One complete target publication followed by the existing 20ms ramps; preserve DSP history and fault counters |
| Valid pre-render restore | Prepare, combined publish, consume targets, reset/snap before the first callback |
| Defensive non-finite input to `setAll` | Reject the entire call; no publication or last-target changes; exactly one rejection-counter increment regardless of bad-field count |
| Finite out-of-range input to `setAll` | Clamp each field, derive once, publish once, update all three last-target values only on success |

Parsing/fallback diagnostics belong to the restore layer: it normally
delivers finite values to `setAll`, so the core's non-finite rejection
counter cannot stand in for a record of corrupt payload fields. Failure
to derive a valid coefficient set must leave accepted controls and
published targets unchanged, matching existing setter behavior.

Version changes use explicit ordered transforms, with defined values for
added fields, explicit rename mappings, and discarded removed fields.
Do not invent a version 0 or accept undocumented legacy parameter-tree
state as version 1. Version 1 has no older supported custom schema yet;
an unknown older version is not an implicit migration. Changing a fixture
is a compatibility/stamp event, not a schema-version event. Future schema
changes need their own migration fixtures and authorization.

## Thread ownership and lifecycle contract

1. Decode, validate, compare fixture identity, and form an immutable full
   tuple on a non-render, non-reentrant restore context. Serialize multiple
   host callers into one logical StateRestore producer; never let host
   callbacks call core setters directly.
2. Transfer a complete tuple through the StateRestore mechanism. Its
   ownership/lifetime protocol must be reviewed before choosing code. The
   render thread must never parse dictionaries, wait for the controller,
   allocate, lock, or retry without a bound.
3. The one Bridge Controller applies pending Host updates, then one
   pending restore via `setAll`, then UI updates. A same-pass UI change may
   override an individual restored value afterward; that accepted ordering
   is distinct from a torn restore. Do not combine the restore into the
   present per-parameter Host/UI winner loop or call three setters for it.
4. Save by taking `controls()`/`getAll()` on that same serialized control
   context, copying the result and applicable fixture identity into an
   immutable save value. Encoding that copy may occur off that context.
   A synchronous host getter needs an explicit reentrancy/deadlock-safe
   controller handoff; it must not read the plain doubles concurrently.
5. A pending restore received before preparation must survive until the
   allocation path can prepare the fixture. With controller mutation and
   rendering excluded, the allocation owner drives `prepare` → controller
   application of the pending complete tuple → `checkForNewTargets` →
   whole-path `reset`. This is the ADR-011 Design note's lifecycle exception,
   not permission for a second concurrent core writer. The private owned
   automation currently has no public path-level consume-and-snap hook;
   a minimal lifecycle-only interface needs review before implementation.
6. Once callbacks can run, restores use the live path and never directly
   call reset from a host/control thread. Preserve ADR-008 §7's independent
   reset flag and zero-frame ordering. Reallocation and deallocation must
   quiesce controller access to preparation/lifetime changes as well as
   rendering. The existing timer is not itself proof of that exclusion.
7. Preserve hybrid bypass correctness: restored Decay/Damp changes must
   reach its existing conservative bound-update path. Exercise running,
   stopped, bypassed and unbypass cases without changing bypass semantics.

## Prerequisites and decisions — unresolved here

These items block an executable implementation plan where indicated.
This document names them for owner/architecture review and chooses no
replacement transport, product policy or encoding.

| ID | Required resolution | Why / affected checkpoint |
|---|---|---|
| SR-P1 | Prove reader ownership and safe reuse of all three restore slots; resolve the ADR sketch if necessary | `(lastPublishedIndex + 1) % 3` can wrap while the Controller still reads a plain-double tuple. Three slots alone do not prevent overwrite/data races. Index/generation association also needs a proof. Blocks transport implementation. |
| SR-P2 | Prove coherent render observation of a whole coefficient publication, including overlapping next publishes | One generation increment after relaxed coefficient stores is a publication notification, not protection against the next writer overwriting fields while `checkForNewTargets()` reads them. No single-increment test alone establishes atomic restore. Any change beyond the accepted existing transport requires explicit architectural scope approval. Blocks atomicity claims and dependent integration. |
| SR-P3 | Confirm lifecycle/control serialization and the minimal path-level combined-set/consume/snap interfaces | Reconcile the sole-controller writer invariant with allocation-time snap; define restore after allocation but before first callback, failed allocation, and reallocation behavior. Blocks AU glue. |
| SR-P4 | Specify encoding and validation at the AU boundary | Key names/envelope, Foundation scalar type checks, malformed/foreign payloads, invalid integer versions, invalid/missing fixture fields, extra keys, and `fullStateForDocument` treatment are not fully specified by ADR-011. Blocks codec/AU work. |
| SR-P5 | Clarify defaults on absent/rejected state during an already-running session | ADR-011 says defaults, but does not fully describe replacement of existing live settings and diagnostics for that case. Do not silently reinterpret rejection as retaining the previous preset or as an immediate snap. Blocks live error-path behavior. |
| SR-P6 | Choose mismatch/error surfacing and any distinct restore notification | Accept-and-surface is decided; the channel is not. A new UI notification channel would amend ADR-008 and is outside this plan. Blocks a claim of complete mismatch handling. |
| SR-P7 | Decide whether the fixture stamp also identifies realized delays | The accepted five fields do not detect a changed delay derivation rule with identical inputs. Do not add a delay hash or claim that hole is closed. Owner decision before freezing format. |
| SR-P8 | Define save visibility and parameter-tree synchronization | Decide whether save includes a pending validated restore or only drained accepted targets, how pre-prepare saves work, and how host-visible values update without feedback into the UI producer or loss of double precision. Blocks AU round-trip contract. |

Plain-integer versioning, per-field fallback, and Host → StateRestore → UI
ordering remain the accepted baseline; this plan does not reopen them.
The owner may separately revisit ADR-011's major/minor alternative or UI
restore event, but neither is needed merely to describe the current scope.

## Proposed file responsibilities

Paths below name future bounded work, not files created in this scoping
pass. Confirm the interface choices after SR-P1…SR-P8 are resolved.

| Files | Responsibility |
|---|---|
| `src/dsp/ParameterAutomation.{h,cpp}` | Combined `setAll` obligation and any separately approved publication correction; reuse existing `getAll` |
| `src/dsp/DiffusionStereoPath.{h,cpp}` | Minimal combined-control forwarding and exclusive pre-render snap surface; preserve private ownership |
| `src/wrapper/ParameterBridge.{h,cpp}` | Restore handoff and ordered controller application; explicit success/failure diagnostic semantics |
| Proposed `src/wrapper/StateSchema.{h,cpp}` | Portable version/fixture/control validation result and explicit migration rules; no Apple types |
| `src/auv3/AetherfieldAudioUnit.{h,mm}` | Native dictionary adapter, state access, lifecycle serialization and diagnostics |
| `tests/ParameterTransitionTests.cpp`, `tests/ParameterBridgeTests.cpp`, proposed `tests/StateRestoreTests.cpp` | Portable behavior and concurrency gates below |
| `CMakeLists.txt`, shared source manifests and `platform/apple/Aetherfield.xcodeproj/project.pbxproj` as required | Register only authorized source/test additions; preserve source-list agreement |

## Checkpoints after separate authorization

### Checkpoint 0 — decisions and baseline

- [ ] Record owner authorization and resolve the blocking SR-P items in
  the appropriate decision record before replacing this scoping plan with
  executable interfaces. Preserve unresolved items as explicit gates.
- [ ] Re-read current source and dirty state. Record exact revision,
  tool/model, changed files, evidence commands and known limits.

### Checkpoint 1 — combined core targets

- [ ] Specify the reviewed `setAll` success/failure return contract,
  mapping reuse, publication ownership and minimal path forwards.
- [ ] Implement only that bounded portable increment after authorization.
- [ ] Gate with SR-1…SR-3 below; independent mechanical verification and
  consequential concurrency review precede its completion checkpoint.

### Checkpoint 2 — portable restore transport and schema

- [ ] Implement the reviewed StateRestore ownership mechanism and ordered
  controller application without adding general-purpose producer roles.
- [ ] Implement the version-1 validator and complete-tuple results using
  the resolved SR-P4/P5/P7 policy; no speculative migration versions.
- [ ] Gate with SR-4…SR-7, then record the portable-only evidence boundary.

### Checkpoint 3 — AU lifecycle and state access

- [ ] Adapt the resolved format to native `fullState`, with controller-safe
  reads, queued restore, exclusive pre-render snap and live retargeting.
- [ ] Integrate diagnostics and host value visibility per SR-P6/P8;
  preserve the accepted custom payload as restore authority if a native
  parameter-tree representation is also carried.
- [ ] Gate with SR-8…SR-10, source-list agreement and Apple compilation.
  A compile alone does not demonstrate host save/recall.

### Checkpoint 4 — documentation and separately scoped host evidence

- [ ] Record implementation, commands/exit status, observed results,
  known gaps and the next action in this plan and `docs/agent-log.md`.
- [ ] Reconcile current status surfaces against actual completed work.
- [ ] Obtain a separate host/device execution scope for ADR-012 HT-7.
  Portable tests do not close it or establish click-free playback.

## Verification gates to design and run later

All gates are **unrun by this plan**. Tests must exercise externally
meaningful outcomes and adversarial schedules, not merely count calls.

| Gate | Required evidence |
|---|---|
| SR-1 Combined values | Defaults, endpoints and representative interior triples; accepted `getAll` values; identical derivation to a directly configured reference; one successful complete publication |
| SR-2 Rejection | NaN/+Inf/−Inf in each field and multiple fields: no target/control mutation, one counter increment; finite clamps independent; derived failure leaves prior state intact |
| SR-3 Coherent consumption | Force writer/reader interleavings during successive contrasting tuples, including a publish between generation load and coefficient loads; every observed target set belongs wholly to one publication; bounded render work; ownership proof plus race instrumentation |
| SR-4 Restore transport | Delay consumer while producer publishes more than three tuples; exercise wraparound and index/generation interleaving; no reader-slot overwrite, torn tuple, unbounded retry or lost newest eligible tuple |
| SR-5 Arbitration | Pending Host/restore/UI conflicts across all controls; exact Host → complete restore → UI order; no per-field interleaving inside restore; deterministic offline block-indexed writes |
| SR-6 Payload validation | Absent payload, absent/invalid controls, finite clamps, missing/newer/invalid/unknown-old version, malformed/foreign types, invalid stamp and fixture mismatch under resolved rules; exact diagnostic outcomes |
| SR-7 Version/fixture separation | Exact version-1 scalar round trip; separate `N`, min/max delay, rate and `D_max` mismatch cases accept normalized values and report; no silent legacy migration; future transforms tested only when a real supported version exists |
| SR-8 Lifecycle | Restore before allocation, after allocation before first callback, live, failed allocation and reallocation; pre-render first-sample identity to directly configured/reset reference; live 20ms ramp without restore-induced history clear; counters preserved |
| SR-9 Save/thread visibility | Host getter/restore callers overlapping controller work; no plain-double race, reentrant queue deadlock or render-thread serialization; precision preserved; parameter-tree synchronization cannot feed the restore back as new UI writes |
| SR-10 Composition | Both supported rates, zero/nonzero blocks, host reset and fault recovery, hybrid running/stopped/bypassed transitions and restored Decay/Damp bounds; no stale stopped state suppresses required processing |

Future portable verification commands, after source/test registration:

```sh
cmake -S . -B build/state-restore -DCMAKE_BUILD_TYPE=Release
cmake --build build/state-restore
ctest --test-dir build/state-restore --output-on-failure
```

Record the exact Apple build and sanitizer commands appropriate to the
then-current targets/toolchain at the relevant checkpoint; do not invent
a successful sanitizer configuration now. Concurrency instrumentation is
evidence about the exercised schedules, not a replacement for SR-P1/P2's
ownership proof. HT-7 later records host versions, callback threads,
restore timing, both rates, malformed state and mismatch diagnostics,
generation/target coherence, and deterministic round-trip rendering.

## Scoping handoff

**Next action:** owner/architecture review of SR-P1…SR-P8 and selection of
the first separately authorized implementation checkpoint. `getAll()`
needs no reimplementation. The atomic set, transport, codec and native
state integration remain unimplemented; no test or HT gate is closed by
writing this plan.
