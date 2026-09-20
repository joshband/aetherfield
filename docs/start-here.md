# Start here

Aetherfield is a portable C++ audio-DSP project progressing toward an iOS/iPadOS ambient reverb. The [product specification](../AETHERFIELD_SPEC.md) defines product intent; it is not evidence that a feature exists. The [roadmap](roadmap.md) defines the currently authorized continuation. Architecture is recorded in the [ADR index](decisions/index.md); implementation plans and verification evidence are separate sources of truth.

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
- **AUv3 wrapper skeleton (`src/auv3/`, `platform/apple/`):** a minimal
  `AUAudioUnit` implementing ADR-010's lifecycle mapping, ADR-008's
  parameter bridge and §7 reset flag, and ADR-009's decided bus/dry-
  passthrough behavior. **Compiles under Xcode only; not yet run under
  `auval`, any host, or any device.** HT-1 through HT-12 remain unrun.
  See `phase1-wrapper-skeleton-plan.md`.

The latest recorded baseline is a Release configure/build and CTest result
from 2026-09-18: **6/6 suites passed**, including DS-13 and the
control-thread API tests. Read [testing.md](testing.md) for commands,
diagnostics, scope limits, renderer evidence, the standalone DS-A
measurements, DS-B Task 4's full measured record, and all three Sonic
acceptance listening rounds plus Sol's review.

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
