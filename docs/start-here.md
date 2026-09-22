# Start here

Aetherfield is a portable C++ audio-DSP project progressing toward an iOS/iPadOS ambient reverb. The [product specification](../AETHERFIELD_SPEC.md) defines product intent; it is not evidence that a feature exists. The [roadmap](roadmap.md) defines the currently authorized continuation. Architecture is recorded in the [ADR index](decisions/index.md); implementation plans and verification evidence are separate sources of truth.

Public hiring/engineering showcase (GitHub Pages from `/docs`):
[index.html](index.html) (hiring skim) · [engineering.html](engineering.html)
(engineering case study) · https://joshband.github.io/aetherfield/ (enable
Pages from branch `/docs`). Listen/audio players are not on the public site
for now; `docs/audio/` clips may remain on disk unused. The former Field Notes
page (`docs/site/`) is retired.

## What is implemented

Phase 0 is a portable CMake/C++20 loop with a gain processor, deterministic tests, and a mono offline WAV renderer. Phase 1 has four implemented bounded increments:

- **S1:** the delay-line and lifecycle skeleton.
- **S2:** the fixed FDN late network and its NS-1 through NS-11 checks.
- **PT:** Mix, Decay, and Damp parameter transitions and PT-1 through PT-9 checks.
- **DS-B Task 2b:** a one-sample mono-to-stereo evaluation route through input
  diffusion, FDN pre-step taps, output diffusion and Mix.
- **DS-B Task 3:** wrapper-owned aggregate fault detection and next-block
  whole-path recovery.
- **DS-B Task 4:** measured DS-1..13 evidence (energy/pole/magnitude bounds,
  echo density, full-path decay, interchannel coherence, mono compatibility,
  channel balance/centroids, a propagated whole-chain numerical-safety
  cessation bound, whole-path per-channel magnitude response, determinism,
  cost) against the Task 1-3 baseline, per ADR-006's correction note. **Every
  checkbox in the [DS-B integration plan](phases/phase1-ds-integration-plan.md)
  is now closed**, including DS-10's propagated bound.
- **Control-thread API (2026-09-18, owner-authorized directly, not part of
  Tasks 1-4):** `DiffusionStereoPath::setDecay()`/`setDamp()`/`setMix()`
  forward to its already-owned `ParameterAutomation`. Two tools use it:
  `tools/render_diffusion_stereo` (single ad hoc renders) and
  `tools/render_listening_batch` (a fixed batch of isolation/corpus/
  falsification renders into `artifacts/`, three rounds of which are
  recorded in testing.md).
- **DS-B Sonic acceptance component: CLOSED (2026-09-18).** Three owner
  listening rounds, a new DS-13 whole-path L/R magnitude-response
  measurement, and a Sol-level architectural review of consequences are all
  recorded in testing.md. **Verdict: no architectural revision warranted** —
  every ADR-006 "Revisit When" trigger is untripped, and the stereo
  observations round 2/3 surfaced are attributed to an already-named,
  already-accepted structural property (ADR-006 (e)'s disjoint tap support),
  not a defect. This closes evidence-gathering for the DS-B component; it
  authorizes **no further implementation** beyond what is already committed.
  The S2/PT-only Sonic acceptance component (mono network, no diffusion) was
  not carried further and remains as testing.md's round-1 notes left it.
- **Wrapper parameter bridge (`src/wrapper/`):** `ParameterBridge` and
  `ResetRequest`, implementing ADR-008 §1–§7's mailbox and reset-flag
  design; PB-1 through PB-8 pass under `aetherfield_wrapper_tests`. This
  is a portable, host-independent mechanism check only — no AUv3 host has
  run this code; see `phase1-wrapper-skeleton-plan.md`.
- **AUv3 wrapper and merged hybrid bypass implementation (`src/auv3/`,
  `src/wrapper/`, `src/dsp/`, `platform/apple/`; `145a69f`):** a minimal
  `AUAudioUnit` implementing ADR-010's lifecycle mapping, ADR-008's
  parameter bridge and §7 reset flag, and ADR-009's decided bus/dry-
  passthrough behavior, now extended with the merged `T_silence`-bounded
  hybrid bypass path. The clean portable Release tree reports 8/8 CTest suites
  passed; the extension has an unsigned Release build only. `auval`, host, and
  device evidence remain absent; HT-1 through HT-12 remain unrun. See
  `phase1-hybrid-bypass-plan.md` and `testing.md`.

The DS-B-era baseline (2026-09-18) recorded **6/6 suites passed**, including
DS-13 and the control-thread API tests. The last recorded Release
configure/build/CTest result (2026-09-20) was **7/7 suites passed**, including
`aetherfield_wrapper_tests` (PB-1…PB-8). Hybrid Task 4 subsequently recorded
**8/8 suites passed** from `build/hybrid-bypass-reconcile`, including
`aetherfield_hybrid_bypass_tests`; its Xcode evidence is unsigned compilation
only. Read
[testing.md](testing.md) for commands, diagnostics, scope limits, renderer
evidence, the standalone DS-A measurements, DS-B Task 4's full measured
record, and all three Sonic acceptance listening rounds plus Sol's review.

## What is decided, and what is not

ADR-001 through ADR-005 are accepted decisions with bounded implementation status described in each record's YAML metadata and review summary. A fixed FDN is implemented; that does not mean a complete audible product signal path, diffusion, stereo output, host integration, UI, modulation, Freeze, Bloom, Texture, or a final product line count is implemented.

ADR-006 accepts an input-diffusion, injection, output-tap, and stereo
architecture as an **evaluation baseline**, with **Tasks 1-4 fully
implemented and measured** (all checkboxes closed in the [DS-B integration
plan](phases/phase1-ds-integration-plan.md)). Its
[review](phases/phase1-ds-review.md) corrections are resolved by an explicit
contract. Full-chain decay, coherence, silence, channel and per-channel
frequency-response measurements are all recorded in `testing.md`. **Sonic
acceptance for the DS-B component is also now closed** (see above) — this is
a measurement/listening/review conclusion, not new authorization: nothing
about `N`, the delay set, the tap design, or any other ADR-002/ADR-006
decision is reopened, and no product signal path, host integration, UI,
modulation, Freeze, Bloom, or Texture work is authorized by any of this.

**If you are picking this project back up:** the DS-B plan and its Sonic
acceptance component are both fully closed, and there is no queued,
pre-authorized *implementation* task.
[ADR-007](decisions/ADR-007-auv3-integration-comparison.md) — native Apple
APIs versus JUCE for a future AUv3 wrapper — is **accepted (2026-09-18)**.
That acceptance selects an architectural direction only: it authorizes no
wrapper, UI, dependency, or other product implementation.
[ADR-008](decisions/ADR-008-parameter-event-bridge.md) (parameter-event
bridge), [ADR-009](decisions/ADR-009-production-bus-policy.md)
(production bus: stereo-in/stereo-out with sum-to-mono reduction, now
decided as an explicit interim step; bypass CPU-vs-tail-continuity
decided as a hybrid, not-yet-designed approach; multi-channel/surround
decided as not a target; `canProcessInPlace = true` recommended pending
implementation confirmation — all 2026-09-19; kill-tail-on-bypass
explicitly deferred until UI is scoped),
[ADR-010](decisions/ADR-010-device-lifecycle-matrix.md) (sample-rate set
{48kHz, 44.1kHz}; minimum deployment target: current major version minus
one, iOS/iPadOS 26+ — corrected 2026-09-19 from the 25+ originally
recorded, since iOS 27 had already shipped four days before the ADR's
2026-09-18 acceptance, making its own "minus one" computation stale at
the moment of acceptance; see ADR-010's "Correction note"),
[ADR-011](decisions/ADR-011-state-schema.md)
(state payload, versioning, and atomic 3-parameter restore — reopening
ADR-008's alternative (C) for the StateRestore role only), and
[ADR-012](decisions/ADR-012-host-device-acceptance-catalog.md)
(HT-1…HT-12 methodology and device/OS scope policy (D)) are all
**accepted (2026-09-18)**; each still names its own "Remaining decisions"
that only the owner can resolve, and none authorizes wrapper/UI/dependency/
test/CI code. ADR-010's floor is a **rolling policy, not a fixed number**
— re-verify it against Apple's then-current data before writing the
wrapper implementation plan. ADR-011 names two unimplemented
`ParameterAutomation` obligations (a read-back accessor and a combined
`setAll` publish entry point); their design was finalized 2026-09-19
(signatures, thread-safety, `setAll`'s validation contract, pre-render
snap sequencing — see ADR-011's "Design note"), but both remain
unimplemented and unauthorized. ADR-012's device/chip-tier scope is
decided (2026-09-19) as per-family corners — iPhone SE 2nd gen/A13 +
newest iPhone; iPad 8th gen/A12 + newest iPad Pro — though the "newest
available" half of each pair must be re-verified at implementation-plan
time. ADR-009's `T_silence(decay)` closed form and its diffusion-cascade-
drain follow-up are both verified (2026-09-19, ADR-009 "Verification
note"); ADR-008 is amended (2026-09-19, ADR-008 §7) to close ADR-010's
named host-reset-concurrency gap with a wait-free flag. **A first bounded
wrapper implementation plan now exists:**
[phase1-wrapper-skeleton-plan.md](phases/phase1-wrapper-skeleton-plan.md)
— an AUv3 skeleton plus ADR-008's parameter-event bridge only (no bypass
hybrid mechanism, no state schema/restore, no HT-1…HT-12 execution). Like
every prior `phase1-*-plan.md`, writing this plan does not itself
authorize the `.h`/`.cpp`/`.mm`/Xcode-project work it describes; that
remains a separate authorization. Do not infer authorization from the
fact that measurement, comparison, framework acceptance, or plan-writing
is finished — read roadmap.md's current continuation and ask, per this
project's own repeated pattern in testing.md/ADRs of never treating
"measured," "compared," or "accepted" as "authorized to implement."

DS-A, the standalone [allpass primitive](phases/phase1-ds-plan.md), is
**implemented** and independently covered by the fifth CTest suite. It does
not authorize or complete full diffusion/stereo integration. Its primitive
response, energy, allocation and partition evidence is recorded in
`testing.md`; it is distinct from the partial full-chain DS-B evidence and
does not close that plan's remaining gaps.

## Current focus (2026-09-20 handoff)

**Repository provenance (2026-09-21):** this checkout now tracks the public
GitHub remote [`joshband/aetherfield`](https://github.com/joshband/aetherfield)
as `origin`. The current committed host/harness evidence baseline is
`f8267ad` (`Add two-rate AUv3 render harness evidence`). This publication
record is repository provenance only; it changes no implementation or
host/device acceptance status.

ADR-009's hybrid bypass implementation is merged at `145a69f` (implementation
commit `b2d6d65`). This supersedes this document's earlier planning-only
handoff: the control-thread bound/read-back work, portable controller, and
AUv3 integration now exist in the merged source. Task 4 evidence is recorded:
a clean Release configure/build/CTest sequence exited 0 with 8/8 suites
passed, and the DSP source-list drift check exited 0 with seven files matched.
The ordinary signed Xcode Release build exited 65 because no development team
is configured; its unsigned counterpart exited 0. This is compilation evidence
only, not host/device validation.

Known limits remain unchanged: Xcode compilation is not host, `auval`, or
device validation; HT-1 through HT-12 and kill-tail UX remain deferred. No
subsequent implementation work is authorized by this reconciliation.

The owner has authorized **planning only** for two separately bounded follow-on
tracks: [host/device acceptance](phases/phase1-host-device-acceptance-plan.md)
under ADR-012, and [versioned state/atomic restore](phases/phase1-state-restore-plan.md)
under ADR-011. Neither plan authorizes implementation or host/device execution.
The host/device plan keeps HT-7 blocked pending the state-restore prerequisite;
the state-restore plan names its unresolved ownership and publication-safety
decisions before an implementation checkpoint can be authorized.

**ADR-011 core obligations implemented (2026-09-22):** commit `65bf878`
implements the two DSP-library obligations ADR-011 §1/§4 named and the
2026-09-19 Design note finalized: `ParameterAutomation::getAll()` (read-back
accessor, already existed; verified) and `ParameterAutomation::setAll(decay,
damp, mix)` (atomic 3-parameter publish with whole-triple non-finite rejection,
per-field clamping, and single-call `publish()` for atomicity). Both follow
the Design note's recommendations: getAll lives on the core (single source of
truth), and setAll uses whole-triple rejection (not per-field fallback) because
atomicity is the entire purpose of the entry point. All 8/8 CTest suites pass.
These obligations close ADR-011's core requirements only; wrapper-level state
serialization, deserialization, and the publish/checkForNewTargets/reset snap
sequencing remain implementation-plan-level work, not yet authorized.

**Host/device acceptance Task 0 discovery and signing unblock (2026-09-20):**
the owner picked this track as the next focus at an interactive check-in.
Confirmed: HEAD is exactly `145a69f` with no intervening commits (baseline
unchanged); ADR-010's rolling floor was already re-verified today with no
drift (iOS/iPadOS 26+ holds); the owner has general physical access to the
full oldest/newest iPhone/iPad matrix and at least one licensed AU host
(AUM and/or Cubasis 3) on target devices; Apple Developer Program enrollment
is complete (Team ID `W2VVZU52J6`). With explicit owner go-ahead,
`platform/apple/project.yml` now carries `DEVELOPMENT_TEAM`/
`CODE_SIGN_STYLE: Automatic`, and **both `AetherfieldAUExtension` and
AetherfieldHost now build and sign successfully** (`testing.md`, "Signed
Release build unblocked"), closing the prior exit-65 signing blocker. This
is still compile-only evidence: no provisioning profile exists locally yet,
no device install has been attempted, and HT-1 through HT-12 remain
entirely unrun. Concrete device/OS/host-app identities for the four matrix
rows still need to be recorded at actual session time. See the plan's
Task 0 checklist for exact per-item status.

**Deferred-device follow-up, same day:** with the physical-device leg
explicitly deferred, a Simulator build/install attempt found that
`AetherfieldHost` (the empty container app) had **no Mach-O executable at
all** — `sources: []` in `project.yml` meant nothing ever linked a binary,
so the earlier "builds and signs cleanly" result only meant `codesign`
didn't error on an empty bundle, not that it was installable anywhere,
device or simulator. This was the exact "container needing code to launch
... blocks this step pending bounded repair" condition Task 1 had already
named. With owner authorization, a minimal fix was applied — a bare,
behavior-free `main.m`/`UIApplicationDelegate` under the new
`platform/apple/AetherfieldHost/` — and verified: both builds now link a
real executable, and **the container now installs successfully on
Simulator**, with the embedded extension independently visible to the OS
plugin registry (`pluginkit -m`). This is registration evidence, not
HT-1's full "discovers, instantiates and renders" (no host had queried
`AVAudioUnitComponentManager` or instantiated the `AUAudioUnit` yet), and
physical-device install remains unattempted. See `testing.md`'s "Bounded
repair applied and verified" section and the plan's Task 1 status for
exact commands/output.

That last gap was closed the same day, owner-requested: a minimal XCTest
harness (`AetherfieldHarnessTests`, hosted in `AetherfieldHost`) now
actually queries `AVAudioUnitComponentManager` and instantiates the
`AUAudioUnit` out-of-process on the simulator — passed, with the runtime
log showing real discovery (`name=Reverb manufacturerName=Aetherfield`)
and instantiation as `AUAudioUnit_XH` (Apple's genuine out-of-process XPC
proxy class). Still not HT-1 itself: no render call, single instance,
Simulator only. See `testing.md`'s "AVAudioUnitComponentManager/AUAudioUnit
instantiation harness" section.

**Two follow-ons, same day, dispatched as parallel file-scope-isolated
subagents (not worktree-isolated — too much of the above was still
uncommitted for a worktree to see it):** (a) a render attempt — format
negotiation and `allocateRenderResourcesAndReturnError:` both succeed, but
the actual `renderBlock` call **fails** with `kAudioUnitErr_RenderTimeout`
(`-66745`), a genuine unresolved problem, not progress toward HT-1/HT-3;
(b) the three build warnings named above are now resolved and
re-verified by rebuild on both device and simulator (0 warnings), with
install/`pluginkit` registration re-confirmed unaffected. Both results
were independently re-verified together (not just trusted from each
subagent's own report) by rebuilding with both changes present: 0
warnings, discovery/instantiation still passes, the render failure
reproduces identically. See `testing.md`'s "Two follow-ons dispatched in
parallel" section for full detail, and the plan's Task 1 status for
per-item state.

**Subsequent AVAudioEngine render-context spike, same day:** owner-approved,
Simulator-only `AVAudioEngine` manual/offline test setup and its one 512-frame
render request both returned success (targeted XCTest: 1 executed, 0 failures),
where the raw XCTest-thread `renderBlock` call returns `-66745`. This makes an
engine-managed graph the stronger direction for the next harness iteration,
but it does **not** establish a working AU render callback or explain the
timeout: the same run logged the out-of-process plug-in connection interrupted
and invalidated, and there is no callback instrumentation or sample-value
oracle. The original direct-call failure remains reproducible and unchanged;
HT-1/HT-3 remain unrun. See `testing.md`'s "(c) AVAudioEngine offline-render
experiment" and the host/device plan's Task 1 status.

**AVAudioEngine oracle refinement, same day:** the prior status-only test was
replaced with a meaningful 4,096-frame impulse test that requires a delayed wet
output (default Mix is wet-only; the FDN's 27 ms minimum delay means ordinary
passthrough cannot pass). It **fails**: the engine reports all frames rendered
but `latePeak=0`, and the plug-in connection is again interrupted. This is a
reproducible end-to-end engine-render failure, still not a localized root
cause. Preserve the red diagnostic; the next bounded investigation is the
extension/XPC lifecycle boundary, not HT-1/HT-3 expansion or a speculative
render repair.

**Crash-report confirmation (2026-09-21):** that lifecycle investigation is
now complete. The fresh Simulator crash report records `EXC_BAD_ACCESS`/
SIGSEGV at address zero on the out-of-process audio render server, symbolicated
to `AetherfieldAudioUnit.mm:514` — the first dereference of the
closure-captured `inputBufferList`. `internalRenderBlock` captures pointers
whose storage is allocated only later by
`allocateRenderResourcesAndReturnError:`, so a host that caches the render
block before allocation leaves a permanently null closure capture. The XPC
interruption and zero output are consequences of that confirmed extension
crash, not output routing or DSP behavior. The raw direct-render timeout
remains separately unresolved. A lifecycle-safe repair needs explicit new
implementation authorization.

**Lifecycle repair and verification (2026-09-21):** authorized implementation
replaced allocation-time raw-pointer capture with AU-lifetime atomic slots that
the cached render block loads per callback; allocation publishes fully formed
resources and teardown withdraws them before release. The existing Simulator
engine impulse oracle is now green (`latePeak=0.014125` at 48 kHz and
`0.014429` at 44.1 kHz after 4,096 frames, without XPC interruption) and
repeats `{1,13,64,512,3}` across 34 requests at both rates; the direct
512-frame render now returns `status=0`. This resolves the reproduced lifecycle
crash/timeout in the Simulator harness only; it does not close physical-device
HT-1/HT-3 acceptance.

**Cached-block reallocation regression (same day):** the direct harness now
caches `renderBlock` before allocation and uses the unchanged closure before
and after deallocation/reallocation; both 512-frame calls return `status=0`.
This protects the fixed lifecycle boundary across reconfiguration, still only
on the Simulator and not as HT-1/HT-3 acceptance.

**Current checkpoint (2026-09-21):** the corrected HT-3 harness has now
rerun on the physical iPhone 16 Pro Max — the earlier CoreSimulatorService/
CoreDeviceService destination-discovery failure cleared on its own, no
reboot needed. Result: `{4096}` (the previously harness-bug-blocked set) and
two other partition sets are bit-exact at both rates, confirming the prior
`{4096}` failure was the harness's own buffer defect and not an AU problem.
A **new, reproduced** mismatch appears instead on the fourth partition set
`{0,1,13,64,512,977,1024,3,0}` (the one exercising a zero-frame call),
diverging mid-stream (~1.1k–1.5k samples in) rather than at the boundary.

**Follow-up investigation, same day, now complete as far as this project's
own source can take it:** the identical leading-zero-frame scenario was
reproduced directly against the portable `DiffusionStereoPath` core in
isolation — bit-exact, no divergence — which, together with a direct reading
of every `count == 0` path in the DSP core and of
`AetherfieldAudioUnit.mm`'s `internalRenderBlock`, rules out this project's
own C++ source (DSP core and wrapper alike) as the cause. A new isolation
test pinned the trigger to the zero-frame call specifically (removing it
restores bit-exactness) and found the leading-zero case's divergence onset
(~1191/1296 samples) matches the FDN's own configured 27 ms minimum delay
almost exactly. A further rerun through a real `AVAudioEngine`-managed
offline render graph (not just the direct-call harness) reproduced the
identical mismatch, ruling out "test-harness calling convention" as the
explanation. **The exact mechanism was then located without Instruments**,
by instrumenting the test's own input-pull block (zero production-code risk):
the render call immediately following every `frameCount == 0` request never
invokes the supplied `pullInputBlock` at all, yet still returns `noErr`, with
the output buffer for that call left holding stale/duplicated data instead
of a fresh computation. This project's own `internalRenderBlock` was already
confirmed to call `pullInputBlock` unconditionally for every non-zero
`frameCount` it actually receives, so the only explanation is that Apple's
out-of-process AU proxy silently never delivers that call through to the
extension in the first place. See `testing.md`'s "Portable-core elimination",
"Zero-frame-partition mismatch isolation", "Zero-frame-partition mismatch:
AVAudioEngine rerun", and "Mechanism located, without Instruments" sections
for exact commands/output. This is now root-caused as far as this project's
own visibility allows: a confirmed defect in Apple's own out-of-process AUv3
render-dispatch proxy, not in this project's DSP core or wrapper C++ (both
read in full and confirmed correct for every call they actually receive). No
fix is proposed or authorized — there is nothing in this project's own
source to change. **Cross-checked against two third-party out-of-process
AUv3 extensions already installed on the same device — Eventide's Blackhole
(proprietary non-JUCE framework) and Audio Damage's Eos 2 (JUCE-built) — and
both show the identical pull-skip at the identical positions.** This settles
whether switching Aetherfield to JUCE would avoid the defect: it would not,
since a completely independent, explicitly non-JUCE plugin exhibits the same
failure; the defect is confirmed to live in Apple's own out-of-process
render-dispatch layer, external to any plugin framework choice, and does not
reopen ADR-007. **Owner decision (2026-09-21): record this as an accepted
external constraint.** HT-3 will document this zero-frame limitation in its
results rather than attempt a fix or retry; the defect remains live in Apple's
own system layer and is orthogonal to this project's AU wrapper and DSP
correctness, both verified. Some real hosts do legitimately issue zero-frame
render callbacks (e.g. transport-stopped/idle states), so this is not purely a
synthetic test-harness edge case. The
independent Task 1 baseline remains green: Release CTest **8/8**, DSP source
drift **7 files**, unsigned AU Debug compile succeeded. **Task 0 authorization
completed (2026-09-21): full physical device matrix authorized, AUM as primary
host, execution scope and device/host/OS identities to be captured at session
time per the evidence contract.**

## Lean resume loop

At a milestone boundary, start a fresh session (or clear the prior context),
read this page, and open only the active plan and its linked source sections.
Use narrow `rg`/`sed` reads instead of dumping full archives. Terra handles
bounded implementation, Luna handles independent mechanical verification, and
high-effort architectural review is reserved for Sol-level decisions. Before
switching to Claude Code or another tool, record the exact commands, changed
files, known gaps and next unchecked task in the active plan or agent log.

Resume prompt:

> Read `docs/start-here.md` and the active plan. Inspect only referenced
> source/tests. Continue the next unchecked task, preserve dirty files, and
> report exact verification commands and remaining gaps.

## Read by task

| Task | Read first | Then use |
|---|---|---|
| Understand project intent and allowed scope | [product specification](../AETHERFIELD_SPEC.md), [roadmap.md](roadmap.md) | This page |
| Change the fixed DSP core | [ADR-002](decisions/ADR-002-late-network.md), [ADR-003](decisions/ADR-003-numerical-safety.md), [ADR-005](decisions/ADR-005-evaluation-fixture.md) | [S1 plan](phases/phase1-s1-plan.md), [S2 plan](phases/phase1-s2-plan.md), [testing.md](testing.md) |
| Change parameters | [ADR-004](decisions/ADR-004-parameters.md) | [PT plan](phases/phase1-pt-plan.md) and [testing.md](testing.md) |
| Plan or review diffusion/stereo | [ADR-006](decisions/ADR-006-diffusion-stereo.md) and its [review](phases/phase1-ds-review.md) | [DS-A plan](phases/phase1-ds-plan.md) and [DS-B integration plan](phases/phase1-ds-integration-plan.md); obtain separate implementation authorization |
| Plan or review the AUv3 wrapper skeleton/parameter bridge | [ADR-007](decisions/ADR-007-auv3-integration-comparison.md), [ADR-008](decisions/ADR-008-parameter-event-bridge.md) (incl. §7), [ADR-010](decisions/ADR-010-device-lifecycle-matrix.md) | [Wrapper skeleton plan](phases/phase1-wrapper-skeleton-plan.md); obtain separate implementation authorization |
| Run or assess evidence | [testing.md](testing.md) | Relevant ADR and phase verification plan |
| Work on host/UI/product features | [ADR-001](decisions/ADR-001-portable-core.md) and [roadmap.md](roadmap.md) | Obtain explicit authorization |

Use source code to establish executable behavior, tests and testing.md for verified results, ADRs for accepted design constraints, plans for proposed work, and the roadmap for authorization state. When these disagree, do not silently reconcile them: record the discrepancy and resolve it before claiming completion.
