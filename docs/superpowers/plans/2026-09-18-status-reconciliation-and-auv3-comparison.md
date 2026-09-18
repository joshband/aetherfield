# Status Reconciliation and AUv3 Comparison Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align maintained status documents with closed DS-B work and create a proposed, documentation-only native-Apple-versus-JUCE AUv3 comparison.

**Architecture:** Current-state documents derive facts from `docs/start-here.md`, the closed DS-B plan, `docs/testing.md`, and source/tests. ADR-007 records a proposed owner decision without adding an Apple or JUCE layer.

**Tech Stack:** Markdown, static HTML, Git, CMake/CTest, official Apple and JUCE documentation.

**Spec:** `docs/superpowers/specs/2026-09-18-status-reconciliation-and-auv3-comparison-design.md`

## Global Constraints

- Documentation and research only: do not edit `src/`, `tests/`, `tools/`, or `CMakeLists.txt`.
- Preserve the portable C++ core, AUv3-only scope, and ADR-001/006 boundaries.
- Keep implemented, measured, planned, proposed, deferred, and accepted states distinct.
- Edit `AETHERFIELD_SPEC.md` only for a concrete stale present-state assertion.
- ADR-007 is proposed; only the owner may accept it.
- Do not fetch JUCE, create Xcode projects, make wrappers/UI, run spikes, or introduce CLAP.

---

### Task 1: Reconcile maintained project-status surfaces

**Files:**
- Modify: `README.md`, `docs/roadmap.md`, `docs/architecture.md`, `docs/dsp-design.md`, `docs/decisions/index.md`, `docs/decisions/ADR-006-diffusion-stereo.md`, `docs/phases/phase1-ds-integration-plan.md`, `docs/phases/phase1-ds-review.md`, `docs/testing.md`, `docs/site/index.html`
- Modify if needed: `docs/decisions.md`
- Inspect only: `AETHERFIELD_SPEC.md`, `docs/start-here.md`

**Interfaces:** consumes the design spec and authoritative evidence; produces one consistent account of implementation, evidence, boundaries, and active continuation.

- [x] Inventory contradictory present-tense claims with `rg -n -i 'Task 4|five host test|Sonic acceptance|unimplemented' README.md docs AETHERFIELD_SPEC.md`.
- [x] Update current-state surfaces for completed DS-B Tasks 1–4, DS-1…DS-13, control forwarding, render tools, Sonic closure, and six CTest suites; preserve implementation prohibitions.
- [x] Review `AETHERFIELD_SPEC.md`; edit it only for a concrete stale present-state assertion, otherwise record why it remains untouched.
- [x] Verify stale current-state claims are absent with `rg -n -i 'Task 4 measurements need separate authorization|five host test suites|Full diffusion and stereo remain planned|Sonic acceptance.*In progress' README.md docs --glob '*.md' --glob '*.html'` and manually classify remaining historical matches.

### Task 2: Add proposed ADR-007 and decision-only evaluation plan

**Files:**
- Create: `docs/decisions/ADR-007-auv3-integration-comparison.md`
- Modify: `docs/decisions/index.md`, `docs/decisions.md`, `docs/roadmap.md`
- Create: `docs/superpowers/plans/2026-09-18-auv3-integration-evaluation.md`

**Interfaces:** consumes ADR-001, Task 1's reconciled status, this design spec, and official-source research; produces canonical proposed ADR and a separate documentation-only evaluation plan.

- [x] Collect and cite official Apple AUv3/`AUAudioUnit` and official JUCE support/licensing evidence; separate it from unperformed host/device validation.
- [x] Write proposed ADR-007 using the existing YAML/header/review-summary pattern. Compare lifecycle, render/parameter/state ownership, UI, build/signing, licensing/dependencies, validation, realtime instrumentation, and format needs. State `status: proposed` and no code authorization.
- [x] Write the decision-only evaluation plan with exact research, owner-decision, and post-acceptance planning checks; prohibit wrappers, UI, scaffolds, dependencies, spikes, CLAP, and host/device claims.
- [x] Add ADR-007 to both indexes and link it from the roadmap; verify repository-relative links.

### Task 3: Verify the package and record its handoff

**Files:**
- Modify: `docs/agent-log.md`
- Inspect: all Task 1 and Task 2 files

**Interfaces:** consumes reconciled documents, ADR-007, and the host build/test loop; produces reproducible evidence and a handoff naming the remaining owner-acceptance gate.

- [x] Run `git diff --check` and confirm ADR-007 stays proposed with explicit non-authorization boundaries.
- [x] Run `cmake -S . -B build/docs-auv3 -DCMAKE_BUILD_TYPE=Release`, `cmake --build build/docs-auv3 --parallel`, and `ctest --test-dir build/docs-auv3 --output-on-failure`; record that it does not validate AUv3 host/device behavior.
- [x] Append one dated agent-log row with worktree, roles, changed document classes, commands/results, specification review result, and owner acceptance gate.
- [x] Run `git status --short`, `git diff --check`, and `git diff --stat`; only planned documentation files may differ.
