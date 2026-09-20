---
id: "ADR-010"
status: accepted
accepted: "2026-09-18"
implementation: "none; architectural decision only, no wrapper/UI/dependency code"
review: "owner-accepted-2026-09-18"
review_document: null
depends_on: [ADR-001, ADR-003, ADR-004, ADR-005, ADR-007]
---

# ADR-010 — Supported device/OS/rate/block-size matrix and AUv3 lifecycle/failure scope

## Review summary

- **Decision:** define, at a decision level, the device/OS/sample-rate/block-size matrix the future AUv3 wrapper claims support for, and the lifecycle/resource/failure-handling contract the wrapper must sit on top of the already-accepted DSP-core contract (ADR-003(d), DS-B Task 3) without duplicating or contradicting it.
- **Why:** ADR-007 named this matrix and this lifecycle/failure scope as an open prerequisite to any wrapper implementation plan, and no document in this repository has ever enumerated a supported-rate list, a minimum OS version, or a wrapper-level fault-escalation policy.
- **Consequence:** a bounded implementation plan for the wrapper can be authorized against a concrete, honestly-labelled matrix instead of an implicit one; several genuine product/business choices are surfaced explicitly for the owner instead of being decided by omission inside implementation code.
- **Uncertainty:** device/chip-tier claims and whether to expand the sample-rate matrix beyond what has ever been measured remain unresolved by this ADR. No iOS device, simulator, or OS version has ever been tested against any Aetherfield code (ADR-001; testing.md's "cross-platform builds, iOS compilation, simulator/device hosting ... remain unverified").

**Status: Accepted (2026-09-18); floor number corrected 2026-09-19 — see
"Correction note" below.** The owner set the minimum deployment target as
a **rolling "current major version minus one"** policy — concretely
**iOS/iPadOS 26+, not the 25+ originally recorded here** (the original
figure treated iOS 26 as "current major" at acceptance, but iOS 27/iPadOS
27 had already shipped four days earlier, so "current major" was already
27 and "minus one" is 26). Apple's own June 2026 distribution snapshot
(iOS 26/iPadOS 26 at 79%/68% of devices; no newer snapshot exists yet for
iOS 27) remains the best available distribution evidence for this floor.
Pinning to the current major version alone would now mean 27+; the
owner's chosen "minus one" policy, for broader reach, means 26+, since
the API-availability floor (iOS 9.0) placed no constraint either way.
This is a **relative policy, not a fixed number**: because "current
major version" moves forward over time, the concrete floor this implies
will change too, and whoever writes the eventual wrapper implementation
plan must re-check Apple's then-current major version and distribution
data rather than treating "26" as permanent — see "Revisit when." This
record still authorizes no wrapper, UI, dependency, or other
implementation. Acceptance of this ADR does not itself authorize any
code; a separately authorized bounded implementation plan is still
required afterward, exactly as ADR-007 already required, and as
[ADR-008](ADR-008-parameter-event-bridge.md) and
[ADR-009](ADR-009-production-bus-policy.md) likewise required for their
own subject matter.

## Correction note — iOS/iPadOS floor recomputed (2026-09-19)

An independent research task (2026-09-19, one day after acceptance)
found that **iOS 27/iPadOS 27 publicly released 2026-09-14 — four days
before this ADR's own 2026-09-18 acceptance date**
([MacRumors](https://www.macrumors.com/2026/09/09/apple-announces-ios-27-release-date/),
[AppleInsider](https://appleinsider.com/articles/26/09/09/ios-27-arrives-on-september-14-heres-what-youll-get),
[Wikipedia](https://en.wikipedia.org/wiki/IOS_27)). The "Status" text and
"(a) Minimum iOS/iPadOS version" section below computed the "current
major version minus one" floor as iOS/iPadOS 25+, treating iOS 26 as
"current major" as of acceptance. But iOS 27 had already shipped by
then, so under this ADR's own unchanged rolling rule, "current major
version" was already 27 at the moment of acceptance — making "current
major version minus one" **iOS/iPadOS 26+, not 25+**. This was a
miscalculation present at acceptance, not new drift accumulated since.

Separately: Apple's major-version numbering skips 19 through 25 entirely
(jumping 18 → 26 → 27), so "25" was never a released OS — no device has
ever run it. Because of that gap, a literal deployment-target string of
"25.0" happens to admit exactly the same device population as "26.0"
would today (the lowest real OS satisfying `>= 25.0` is 26.0), so this
correction does not change which physical devices are supported *right
now*. It matters because leaving "25" recorded as the anchor would
compound: a future re-verification starting from "current major = 26"
(instead of the correct 27) would again be one full generation stale
before any further time passes.

**Wherever this record says "iOS/iPadOS 25+," read "iOS/iPadOS 26+."**
This note is part of this ADR's current contract; where it conflicts
with dated language elsewhere in this record, this note governs, per
this project's existing correction-note convention (see ADR-006). The
rolling policy itself — current major version minus one, re-verified at
wrapper-implementation-plan time — is unchanged and not reopened by this
note, nor is any other section of this ADR (sample rates, block-size
handling, lifecycle mapping, HT-1…HT-12). No newer Apple
adoption-distribution snapshot than the June 2026 figures exists yet for
iOS 27, since a new major's first adoption chart typically lands months
after release; the 79%/68% figures remain the best available
distribution evidence even though they describe iOS 26 rather than the
now-current iOS 27.

## Context and scope

ADR-007 (accepted 2026-09-18) selected native Apple AUv3 APIs as the integration direction and listed, in "Remaining decisions and later evidence," an open prerequisite list that includes "supported device/OS/rate/block-size matrix" and "resource and failure behavior," alongside its evidence-table finding that "AUv3 hosts can call render with variable-length or zero-length blocks." Those two items are this ADR's entire scope. It does not reopen the parameter-event bridge ([ADR-008](ADR-008-parameter-event-bridge.md)), production bus/state schema ([ADR-009](ADR-009-production-bus-policy.md) and the still-unassigned state-schema ADR), UI toolkit, signing/identifiers, or licensing — all remain open exactly as ADR-007 left them.

This ADR also does not reopen anything ADR-001, ADR-003 or ADR-005 already decided. ADR-001 fixed the portable-core boundary: no Apple or JUCE types in `src/dsp/`, and the only verified host loop remains CMake/CTest — no iOS device, simulator, or OS version has ever run any Aetherfield code. ADR-003(d) already fixed the `prepare`/`reset`/sample-rate-change contract at the DSP-core level, including what a failed `prepare` guarantees ("changes nothing... a prior valid preparation remains valid and usable"), what `reset()` must zero and must not touch, and the finite-time-silence and non-finite-detector guarantees. ADR-005 already fixed the only two sample rates this project has ever measured anything at — 48 kHz and 44.1 kHz — and explicitly left the product's supported-rate matrix as "a real and wider gap... entangled with ADR-001's deferred AUv3 framework decision." This ADR is where that gap is partially closed, on the evidence that exists, not on new measurement: no new fixture, rate, or device evidence was produced while drafting it.

The wrapper contract described below must sit *on top of* the DS-B Task 3 fault/recovery contract already implemented in `DiffusionStereoPath` (docs/phases/phase1-ds-integration-plan.md, docs/start-here.md): "wrapper-owned aggregate fault detection and next-block whole-path recovery," where cumulative fault counts survive `reset()` and re-`prepare()`, a fault "only schedules the next nonempty-block reset; it never changes the current sample by an undocumented autonomous reset," and a zero-frame call "neither processes audio nor consumes a pending reset." This ADR does not redesign or duplicate that contract; it states how an AUv3 host-facing wrapper maps onto it.

## Evidence basis

No new measurement was performed for this ADR. Every factual claim below is either (a) a quotation or direct restatement of an already-accepted ADR or already-recorded test result, (b) a documentation-only Apple API fact already cited in ADR-003/ADR-007's evidence tables, or (c) the result of a bounded, owner-authorized, documentation-only research task (2026-09-18) into the minimum iOS/iPadOS version each API this design cites actually requires, verified against Apple's own primary-source developer documentation (see "(a) Minimum iOS/iPadOS version" below for the findings and their source basis). ADR-007 explicitly distinguished "the archived extension guide establishes packaging context, not current deployment-version eligibility"; that specific gap is now closed for the API-availability question (every cited API needs only iOS 9.0), though the owner's actual market-reach floor decision remains open.

| Source | What it grounds in this ADR |
|---|---|
| ADR-005 (c) | The only two sample rates ever measured in this project: 48 kHz (primary) and 44.1 kHz (secondary), as an S2/DS-B *fixture* scope explicitly not yet a product commitment. |
| ADR-003 (d) | `prepare`/`reset`/sample-rate-change guarantees: allocation only at `prepare`, off the render thread; a failed `prepare` changes nothing; `reset()` is noexcept, allocation-free, zeros full reserved capacity, clears the non-finite latch but never the counter; a rate change is a full re-`prepare` and discards the tail. |
| DS-B Task 3 (docs/phases/phase1-ds-integration-plan.md; docs/start-here.md) | The wrapper-level `nonFiniteCount()`/`nonFiniteLatched()`/`resetPending` contract: cumulative counts survive reset/re-prepare; a fault schedules only the *next nonempty block's* full reset; a zero-frame call consumes nothing. |
| ADR-007 evidence table | AUv3 hosts may call render with variable-length or zero-length blocks; `allocateRenderResourcesAndReturnError:` runs before rendering starts, `deallocateRenderResources` after it finishes (already cited in ADR-003 (d)'s "AUv3 mapping" note). |
| testing.md | "Current host verification is macOS arm64 only... cross-platform builds, iOS compilation, simulator/device hosting, sample-rate lifecycle and render-thread instrumentation remain unverified." Xcode 27.0 (27A266a) is locally present and version-checked only; this is explicitly not an iOS build or device test. |
| src/dsp/ (this ADR's own grep, 2026-09-18) | No `thread_local`, no mutable file-scope/global state, no singleton pattern, and no non-`constexpr` `static` data anywhere in `src/dsp/*.h`/`*.cpp` (only `static constexpr` compile-time constants and one `static` member function, `ParameterAutomation::stepRamp`). Every class inspected — `DelayLine`, `FeedbackDelayNetwork`, `ParameterAutomation`, `DiffusionStereoPath`, `SchroederAllpass`, `GainProcessor` — holds its state in instance members (including `ParameterAutomation`'s per-instance `std::atomic` targets and `DiffusionStereoPath`'s `state_`); `DiffusionStereoPath.h` additionally `= delete`s both its copy constructor and copy assignment operator while providing explicit move construction/assignment, consistent with a type designed to be owned uniquely per instance rather than shared. This is a source-inspection finding, not a tested multi-instance guarantee — see "Multiple concurrent instances" below. |

## Scoping matrix

### (a) Minimum iOS/iPadOS version

This is a genuine reach-versus-maintenance-cost tradeoff this ADR does not settle. A bounded, documentation-only research task (owner-authorized 2026-09-18, Apple-primary-source only, no code and no ADR decision made by the researcher) has since closed the API-availability half of this question. Findings, verified against each API's raw DocC JSON metadata on developer.apple.com, not just the rendered page:

| API / concept | Min iOS/iPadOS (Apple-stated) | Deprecated? |
|---|---|---|
| `AUAudioUnit` (class) | iOS 9.0 | No |
| `allocateRenderResources()` / `deallocateRenderResources()` | iOS 9.0 | No |
| `AUParameterTree` | iOS 9.0 | No |
| `fullState` | iOS 9.0 | No |
| `shouldBypassEffect` | iOS 9.0 | No |
| `canProcessInPlace` | iOS 9.0 | No |
| `maximumFramesToRender` | iOS 9.0 | No |
| `reset()` | iOS 9.0 | No |
| `internalRenderBlock` / `AUInternalRenderBlock` | Not stated in Apple's docs (no `introducedAt` in the page's own metadata) | No |
| `AUParameterEvent` / `AUParameterObserver` | Not stated in Apple's docs (same gap) | No |

**Hard floor: iOS/iPadOS 9.0** — the release that introduced AUv3 itself. Every cited API for which Apple states a version requires exactly 9.0; no single API is a stricter binding constraint than the rest. This makes the API-availability question moot for any deployment decision plausible in 2026: nothing in the cited AUv3 surface pushes the floor above ancient hardware. Four APIs (`internalRenderBlock`, `AUInternalRenderBlock`, `AUParameterEvent`, `AUParameterObserver`) have no Apple-stated version at all — a gap in Apple's own documentation, not filled with an assumption here.

**The real constraint is market reach and Apple's build-toolchain policy, not API availability.** Separately researched, both primary-source:

- **Build-toolchain policy (distinct from deployment target):** Apple's official minimum-SDK notice states that, effective **April 28, 2026**, all new App Store submissions/updates must be **built with the iOS 26 / iPadOS 26 SDK or later** — this governs the SDK Xcode builds with, not the app's chosen minimum deployment target; a project can build with the iOS 26 SDK while still setting a much lower deployment target.
- **Adoption data (informational, not a recommendation):** Apple's own distribution page (snapshot 2026-06-07) publishes only current-major-version adoption, not a per-version breakdown: iOS 26 = 79% of all devices (86% of devices ≤4 years old); iPadOS 26 = 68% of all devices (79% of devices ≤4 years old). Apple does not publish a "current−1/−2/−3" breakdown; only "≈21% of iOS / ≈32% of iPadOS devices are on iOS/iPadOS 25 or older" can be derived from Apple's own numbers, with no finer split available. Third-party trackers publish more granular estimates, but none of those are Apple's own data and are not used here as fact.

| Consideration | What it means for a lower minimum | What it means for a higher minimum |
|---|---|---|
| Market reach | Larger addressable device/OS population; below iOS 25 the only data available is third-party, not Apple's own | Smaller; iOS 26+ alone is 79%/68% of devices per Apple's own June 2026 snapshot |
| `AUAudioUnit` API surface actually used | No longer a constraint — the hard floor is 9.0 regardless of the owner's choice | Same — no cited API benefits from a higher floor |
| Build toolchain | Independent of deployment target — Xcode must build with the iOS 26 SDK from 2026-04-28 regardless of what floor is chosen | Same requirement either way |
| Maintenance cost | More `@available`/version-guard code the further below current the floor sits, though nothing here requires it for the *cited* API set specifically | Single code path, less conditional logic |
| Device/chip generation correlation | A given iOS floor implicitly admits or excludes certain device/chip tiers (a related, separately flagged product choice below) | — |

**Decided (2026-09-18); number corrected 2026-09-19 (see "Correction
note" above):** minimum deployment target is **current major version
minus one** — concretely **iOS/iPadOS 26+**, not the 25+ originally
recorded here, because iOS 27/iPadOS 27 had already shipped
(2026-09-14, four days before this ADR's 2026-09-18 acceptance), making
"current major version" 27 at acceptance and "minus one" 26. Apple's own
June 2026 snapshot (iOS 26/iPadOS 26 at 79%/68% of devices) remains the
best available distribution evidence for this floor; no iOS 27-specific
snapshot exists yet. The API-availability research removed the only axis
that could have forced a specific number (nothing cited requires more
than iOS 9.0); the owner chose broader reach over pinning to the current
major version alone (which would now be 27+). Because this is a rolling
policy rather than a fixed number, the concrete floor must be
re-verified against Apple's then-current major version and distribution
data at the time the wrapper implementation plan is actually written —
see "Revisit when."

### (b) Sample rates

**Decision (proposed):** the product's initial supported sample-rate set is exactly **{48 kHz, 44.1 kHz}** — the two rates ADR-005 fixed for the S2/DS-B evaluation fixture and the only two rates any Aetherfield code has ever been measured at (testing.md's DS-1…DS-13, NS-1…NS-11, PT-1…PT-9 results are all reported at one or both of these rates and no other). This is an evidence-availability decision, not a business one: no other rate has ever had a delay set, coefficient set, or measured decay/coherence/mono-compatibility result derived for it.

This does not foreclose other rates. ADR-005 (b)'s derivation rule (geometric delay-length spacing, nearest-prime snap, mutual co-primality check) is rate-parametric by construction, and ADR-005 (c) already anticipated exactly this: "the product's supported sample-rate matrix is a wider question it deliberately does not answer." Adding a rate such as 96 kHz or 88.2 kHz — both common in professional host sessions — requires, at minimum: (1) running ADR-005 (b)'s derivation at that rate, (2) re-running the NS/PT/DS-1…13 measurement suite at that rate (not merely allowing `prepare()` to succeed there), and (3) reassessing ADR-005 (b)'s reserved-capacity ceiling (`C = round(f_s_max · t_max) + 64`), since a new maximum supported rate raises the memory reservation for every line. **Whether market reach justifies that verification cost for a specific additional rate is the owner's call**, flagged below; this ADR only fixes what is *already* evidenced.

**Unsupported-rate behavior:** already decided at the DSP-core level (ADR-003 (c)/(d)): `prepare` fails and prior preparation remains valid and usable. This ADR's addition is only the AUv3-layer mapping: the wrapper's `allocateRenderResourcesAndReturnError:` must translate that boolean/exception outcome into a populated `NSError` and return `NO`, without attempting a silent fallback (e.g., forcing the request to 48 kHz) — a fallback would violate ADR-003 (d) rule 7's transactional guarantee by pretending the requested configuration succeeded. Whether that failure should additionally surface a user-visible diagnostic (versus relying entirely on however the host chooses to present an `AUAudioUnit` allocation failure) is a UX question entangled with the still-deferred UI decision (ADR-007) and is flagged below.

### (c) Block-size handling

**Decision (proposed):** the wrapper treats the host-supplied block size as fully variable per render call, including the documented zero-length case, and adds no new zero-length-handling logic of its own. This is not a new mechanism: DS-B Task 3's required sample order already specifies, verbatim, "At block entry, if `resetPending`, run the full reset... before the first sample. For `count == 0`, return before this step," and the global constraint that "a zero-frame call neither processes audio nor consumes a pending reset." The AUv3 `internalRenderBlock`'s job is only to pass the host's actual `frameCount` through to that existing, already-tested contract — never to special-case zero itself.

`ParameterAutomation::checkForNewTargets()` is consumed once per nonempty block per ADR-004 (c)/(d), so block size determines automation update *granularity*, not correctness; larger blocks mean coarser control resolution, not a new failure mode.

**What is not decided here, and is a genuine gap, not a business tradeoff:** the only block partitions with recorded bit-identical-across-partitions evidence are the fixed set `{1, 13, 64, 512, 3}` and DS-11's ragged partition `{7, 29, 3, 211, 5}` (docs/phases/phase1-ds-integration-plan.md, Task 2; docs/testing.md DS-11). Note the ragged partition already includes a 211-sample step, above the 64-sample fixed partition though still below 512 — so "beyond 512" is the honest boundary of measured territory, not "beyond 64." A host declaring a `maximumFramesToRender` far above 512 is untested territory for partition-invariance; the architecture's block-boundary-only automation model implies the property generalizes, but that is an inference, not a measurement, and is named as a future test category below (HT-3).

## Lifecycle, resource, and failure handling

This section states how the AUv3 wrapper's host-facing lifecycle methods map onto the already-accepted DSP-core contract. It decides mapping, not mechanism: ADR-003 (d) already decided what `prepare`/`reset` guarantee, and DS-B Task 3 already decided the aggregate-fault contract. Nothing below reopens either.

| Wrapper event | Maps onto | What is already guaranteed (cited) | What this ADR adds |
|---|---|---|---|
| `allocateRenderResourcesAndReturnError:` | `DiffusionStereoPath`/FDN `prepare()` | Off-render-thread, allocation-only-here; failure leaves prior live state intact (ADR-003 (d) rule 7; DS-B Task 1's transactional candidate-then-commit preparation) | On failure, populate `NSError` and return `NO`; never substitute a different configuration silently |
| Unsupported sample rate or excessive channel/bus configuration | `prepare()` rejection | Rejected, not clamped (ADR-003 (c)/(d)) | Wrapper performs no additional validation duplicating the core's; channel-count/bus-layout rejection is a `prepare`-time failure category alongside sample rate, though the production bus layout itself is [ADR-009](ADR-009-production-bus-policy.md)'s item, not decided here |
| `-reset` (host-invoked, e.g. transport stop) | `DiffusionStereoPath::reset()` | noexcept, allocation-free, zeros full reserved capacity, clears filter/damping state exactly, clears the non-finite latch but **not** its counter, idempotent, safe pre-prepare (ADR-003 (d)) | Wrapper must not clear the cumulative fault counter on a host-triggered reset either — the "reported, never hidden" rule (DS-B global constraints) applies to the wrapper's exposed diagnostic surface, not only the core's |
| Per-block aggregate fault (non-finite sample observed) | DS-B Task 3's `resetPending`/aggregate latch | Self-healing: the *next nonempty block* is automatically reset from exact whole-path silence; cumulative count survives (docs/phases/phase1-ds-integration-plan.md Task 3) | The wrapper needs no new recovery logic for a single fault — this already exists and is tested |
| Repeated/aggregate faults beyond a single event | — | Not addressed by any accepted ADR | **Undecided, flagged below.** Whether the wrapper additionally signals the host (e.g., an observable `AUParameter`/property, logged diagnostic) on top of the existing self-recovering block-boundary reset, forces some stronger action, or does nothing beyond what already exists, is a genuine product choice this ADR does not make |
| `deallocateRenderResources` | Unresolved — see gap below | RAII teardown of `DiffusionStereoPath`'s owned state exists (destructor exists per source; no static/global state was found to leak across instances), *if* the C++ instance is actually destroyed here | **Named gap, not resolved.** `deallocateRenderResources` tears down render resources; it does not necessarily mean the host destroys the `AUAudioUnit` instance, which may later call `allocateRenderResourcesAndReturnError:` again on the same instance (e.g., across a format change) without an intervening `-reset`. If the wrapper maps this event to destroying and later reconstructing the C++ `DiffusionStereoPath`, the cumulative fault counter — which DS-B Task 3 requires to survive `reset()`/re-`prepare()` — would be silently lost, contradicting the row above. This ADR does not decide whether the wrapper keeps the C++ instance alive across a deallocate/reallocate cycle (which would preserve the counter) or reconstructs it (which would need the counter relocated to the wrapper's own persistent state); that decision is deferred to the bounded implementation plan |

**Host-triggered reset concurrency: resolved by ADR-008 §7 (2026-09-19 amendment), not resolved here.** ADR-003 (d) and DS-B Task 3 both specify `reset()`'s guarantees assuming it is not called concurrently with the render callback from another thread. AUAudioUnit hosts may invoke `-reset` from a thread other than the render thread. This ADR left that gap unassigned, naming exactly two routes to close it — "either ADR-008's scope must be explicitly widened to cover it, or a separate decision must adopt it." [ADR-008](ADR-008-parameter-event-bridge.md) §7 has since taken the first route: a wrapper-owned wait-free `resetRequested_` flag, set by `-reset` on whatever thread the host calls it from, drained and acted on (`DiffusionStereoPath::reset()`) only at the top of the render thread's own callback — never calling `reset()` from the host's thread directly. This ADR's own text is otherwise unchanged; see ADR-008 §7 for the resolved design.

## Multiple concurrent instances

A host may load several instances of the AU simultaneously (e.g., on multiple tracks). This ADR's finding, from directly inspecting `src/dsp/*.h`/`*.cpp` (not from assumption): **no mutable global, `static` (non-`constexpr`), `thread_local`, or singleton state exists anywhere in the portable core.** `DelayLine`, `FeedbackDelayNetwork`, `ParameterAutomation`, and `DiffusionStereoPath` each hold all state in instance members — including `ParameterAutomation`'s atomics, which are per-instance `std::atomic<float>`/`std::atomic<std::uint64_t>` members, not shared statics. `DiffusionStereoPath.h` additionally `= delete`s its copy constructor and copy-assignment operator while declaring an explicit move constructor and move-assignment operator, consistent with a class designed to be owned uniquely per instance rather than shared.

**This supports, but does not prove, multi-instance safety.** No test in this repository constructs and drives two instances of any of these classes concurrently, and nothing in this project's evidence discipline permits treating a source-inspection finding as a measured result (ADR-005/ADR-003's own repeated rule: never claim evidence that does not exist). This ADR states the finding honestly and names the corresponding future test category (HT-9) rather than asserting multi-instance safety as decided.

## Source-list ownership between the portable build and the future Apple build

**Proposed convention**, consistent with ADR-001's portable-core boundary ("no Apple or JUCE types in `src/dsp/`"):

- Portable core files remain exactly where they are, under `src/dsp/`, and continue to be built by the existing CMake/CTest target unchanged.
- The future Apple/Xcode target consumes the **same** `src/dsp/` file list — not a copy — through one explicit, single-sourced list (e.g., a CMake variable or a small manifest file that both the CMake target and the Xcode project generation step read), so the two build systems can never independently drift on which portable files exist.
- All wrapper-side code (the `AUAudioUnit` subclass, factory, Apple-specific glue) lives under a new, separate directory (e.g. `src/auv3/` or `platform/apple/`) created only when that work is separately authorized. It never modifies `src/dsp/` headers to add Apple/Objective-C/Swift types, matching ADR-001's existing rule and ADR-007's "keeping all platform glue outside the portable core."
- Apple build settings, signing, and SDK/deployment-target configuration live entirely in the Apple target's own project files, never inside `src/dsp/`'s CMakeLists.txt.

**What would catch drift:** a CI check (or, until CI exists, a recorded manual verification step per this project's evidence-on-disk discipline — CLAUDE.md's "Keep implementation, verification and handoff evidence on disk") that (1) greps `src/dsp/` for any Apple/Objective-C/JUCE include or type (`#import`, `Foundation/`, `AudioToolbox/`, `juce_`) and fails if found, and (2) confirms the portable CMake target's source list and the Apple target's shared-file list are generated from, or diffed against, the same single manifest rather than maintained as two hand-edited lists. Neither check is implemented by this ADR; both are named so a future implementation plan can pick them up without re-deriving the requirement.

## Future host/device acceptance test categories

Named at the granularity of this project's existing DS-1…13/NS-1…11/PT-1…9 convention, listed here because this matrix implies they must eventually exist — none of them is designed, scoped in detail, or run by this ADR:

- **HT-1** — cold instantiation and teardown (`allocateRenderResourcesAndReturnError:` / `deallocateRenderResources` cycle, repeated)
- **HT-2** — reconfiguration (format/rate change while instantiated; `prepare`-failure rollback observed at the AU boundary)
- **HT-3** — variable- and zero-length render-call handling, including block sizes beyond the `{1,13,64,512,3}` fixed partitions and DS-11's ragged `{7,29,3,211,5}` partition this project has actually measured
- **HT-4** — input-pull failure / host underrun handling
- **HT-5** — bypass and tail behavior (depends on [ADR-009](ADR-009-production-bus-policy.md)'s bypass decisions)
- **HT-6** — reset and aggregate-fault recovery, both host-triggered (`-reset`) and self-triggered (DS-B Task 3's next-block path), including the cross-thread reset-concurrency question named above
- **HT-7** — state save/recall (`fullState` round trip) — blocked on the still-unassigned state-schema ADR; not runnable until that lands
- **HT-8** — dense or conflicting host automation/parameter events (depends on [ADR-008](ADR-008-parameter-event-bridge.md))
- **HT-9** — multiple concurrent instances
- **HT-10** — offline (non-realtime) render repeatability
- **HT-11** — realtime allocation/locking audit on the actual render callback, on-device
- **HT-12** — callback-deadline / worst-case timing measurement on named devices

## Decision and tradeoffs

This ADR fixes: the initial product sample-rate set as {48 kHz, 44.1 kHz} pending new evidence for any addition; the block-size handling model as fully host-variable including zero-length, deferring to the already-implemented DS-B Task 3 contract rather than adding new logic; the lifecycle mapping of `prepare`/`reset`/failure onto `allocateRenderResourcesAndReturnError:`/`-reset`/`NSError`, deferring to ADR-003 (d)'s and DS-B Task 3's existing guarantees rather than duplicating them; the source-list ownership convention and its drift-detection approach; and the named (not designed) future test categories HT-1…HT-12.

It deliberately does not fix: device/chip-tier support claims, whether an unsupported-configuration failure is additionally user-visible, whether to expand the sample-rate matrix for market reach, whether repeated aggregate faults warrant a host-visible signal beyond the existing self-recovering reset, or the cross-thread `reset()` concurrency question. Each is named explicitly in "Remaining decisions" rather than defaulted. (The minimum iOS/iPadOS version is now decided — see "Status" above.)

Accepting this ADR authorizes no wrapper, UI, or dependency code. A separately authorized, bounded implementation plan — naming concrete files, an Xcode/CMake integration approach, and its own test plan — is still required after acceptance, exactly as ADR-007 already required, and as [ADR-008](ADR-008-parameter-event-bridge.md) and [ADR-009](ADR-009-production-bus-policy.md) likewise require.

## Remaining decisions and later evidence

Before a separately authorized wrapper implementation plan: re-verifying the concrete minimum-version number against Apple's then-current major version and distribution data, since "current major version minus one" is a rolling policy, not a fixed number, and the concrete iOS/iPadOS 26+ floor recorded above (corrected 2026-09-19; see "Correction note") reflects only the 2026-09-18/19 snapshot; which device/chip generations to claim support for (a related but separate choice from the OS floor); whether an unsupported-configuration failure (rate, channel count, or otherwise) should be surfaced only through however the host presents an `AUAudioUnit` allocation error, or additionally through a user-visible diagnostic once UI is authorized; whether market reach justifies verifying and shipping support for a sample rate beyond {48 kHz, 44.1 kHz} given the ADR-005-style re-derivation and re-measurement cost that would require; whether repeated aggregate faults should escalate beyond the existing self-recovering next-block reset (e.g., an observable diagnostic property) or whether the current DSP-core behavior is sufficient as-is; the cross-thread `reset()`-versus-render-callback concurrency question, **now resolved by ADR-008 §7's wait-free `resetRequested_` flag** (2026-09-19 amendment); and whether the wrapper keeps the C++ `DiffusionStereoPath` instance alive across a `deallocateRenderResources`/reallocate cycle (preserving the cumulative fault counter) or reconstructs it (requiring the counter to move to the wrapper's own persistent state), named above as an unresolved gap in the lifecycle table.

An eventual verification plan must run HT-1 through HT-12 above on named devices/hosts/OS versions once the minimum-version decision is made; none of that evidence exists today, and this documentation-only ADR produces none of it.

## Revisit when

Significant time passes before the wrapper implementation plan is written, requiring the concrete iOS/iPadOS 26+ floor (corrected 2026-09-19; see "Correction note") to be re-verified against Apple's then-current major version and distribution data under the accepted "current minus one" policy; the owner sets a device/chip-tier scope; ADR-005's own derivation method is run at a new sample rate and its NS/PT/DS-1…13 measurement suite is repeated there, making that rate a candidate for the supported set; [ADR-008](ADR-008-parameter-event-bridge.md) lands and also resolves (or explicitly declines to resolve) the `reset()`/render-callback concurrency question, at which point this ADR's open item is superseded rather than independently re-argued; the state-schema ADR lands, unblocking HT-7; a bounded wrapper implementation plan is authorized and HT-1…HT-12 begin producing real device/host evidence that may contradict any assumption recorded here (particularly the multi-instance source-inspection finding, which this ADR explicitly does not claim as tested); or a host is observed requesting a block size outside the `{1,13,64,512,3}` fixed partitions and DS-11's ragged `{7,29,3,211,5}` partition this project has actually measured, at which point HT-3 becomes actionable rather than named.
