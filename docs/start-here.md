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

The latest recorded baseline is a Release configure/build and CTest result from
2026-09-17: **6/6 suites passed**, including DS-B Task 3 fault/recovery coverage.
Read [testing.md](testing.md) for commands, diagnostics, scope limits,
renderer evidence and the standalone DS-A measurements.

## What is decided, and what is not

ADR-001 through ADR-005 are accepted decisions with bounded implementation status described in each record's YAML metadata and review summary. A fixed FDN is implemented; that does not mean a complete audible product signal path, diffusion, stereo output, host integration, UI, modulation, Freeze, Bloom, Texture, or a final product line count is implemented.

ADR-006 accepts an input-diffusion, injection, output-tap, and stereo
architecture as an **evaluation baseline**, with **Task 1
lifecycle/preparation, Task 2a's read-only FDN pre-step taps, Task 2b's
one-sample route, and Task 3 recovery implemented**. Its
[review](phases/phase1-ds-review.md) corrections are resolved by an explicit
contract and the bounded [DS-B integration plan](phases/phase1-ds-integration-plan.md).
Full-chain decay, coherence, silence and channel measurements remain
unimplemented.

DS-A, the standalone [allpass primitive](phases/phase1-ds-plan.md), is
**implemented** and independently covered by the fifth CTest suite. It does
not authorize or complete full diffusion/stereo integration. Its primitive
response, energy, allocation and partition evidence is recorded in
`testing.md`; the full-chain DS evaluation plan remains unimplemented.

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
| Run or assess evidence | [testing.md](testing.md) | Relevant ADR and phase verification plan |
| Work on host/UI/product features | [ADR-001](decisions/ADR-001-portable-core.md) and [roadmap.md](roadmap.md) | Obtain explicit authorization |

Use source code to establish executable behavior, tests and testing.md for verified results, ADRs for accepted design constraints, plans for proposed work, and the roadmap for authorization state. When these disagree, do not silently reconcile them: record the discrepancy and resolve it before claiming completion.
