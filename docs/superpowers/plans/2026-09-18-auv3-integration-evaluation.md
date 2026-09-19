# AUv3 integration decision-only evaluation plan

**Status:** ADR-007/008/009/010/011/012 all accepted. Only the wrapper implementation plan itself remains unstarted. **No implementation authorized.**

**Purpose:** decide whether native Apple APIs or JUCE should later wrap the
portable C++ core for iOS/iPadOS AUv3. The proposed outcome and primary-source
comparison are canonical in [ADR-007](../../decisions/ADR-007-auv3-integration-comparison.md).
The [roadmap](../../roadmap.md) controls scope.

## Read only the governing records

- [ADR-001](../../decisions/ADR-001-portable-core.md): Decision, Consequences and Revisit When.
- [ADR-004](../../decisions/ADR-004-parameters.md): Decision (b), (c), (d), Consequences and Revisit When.
- [ADR-006](../../decisions/ADR-006-diffusion-stereo.md): review summary and evaluation-versus-product boundary.
- [testing.md](../../testing.md): current host baseline, DS-B closure and Sonic acceptance record; these are not device/host certification.
- [ParameterAutomation](../../../src/dsp/ParameterAutomation.h) and [DiffusionStereoPath](../../../src/dsp/DiffusionStereoPath.h): control-thread, lifecycle and render contracts.

## Scope fence

Research, documentation review and owner decision only. Do not create a wrapper,
UI, scaffold, Xcode project, identifier, signing configuration or build target.
Do not fetch/install JUCE, add dependencies, run a spike or implement CLAP.
Do not change DSP/tests/tools/CMake. Do not run or claim AUv3 host/device
validation under this plan. Existing host CTest results remain host evidence.

## 1. Documentation research checkpoint

- [x] Compare native and JUCE lifecycle, render/bus/parameter/state ownership, UI, build/signing, dependencies/licensing, validation, realtime instrumentation and actual format needs in ADR-007.
- [x] Cite official Apple contracts and exact JUCE 8.0.12 documentation/source/licence; label that release a comparison specimen, not a dependency decision.
- [x] Separate documented framework capabilities from project judgments and unperformed measurements. Record no claims about relative binary size, CPU, development speed or host reliability.
- [x] Preserve the portable C++ core, AUv3-only scope, deferred UI and ADR-004's block-quantized 20 ms internal ramp semantics.
- [x] Identify the single-writer/non-realtime-safe setter boundary and require a bounded nonblocking event bridge decision before any wrapper plan.
- [x] Keep production buses, schema/version/migration/restore policy and product DSP configuration open; acknowledge native `fullState` parameter-tree defaults.
- [ ] Record independent review of ADR-007, resolve findings, and recheck official sources if the decision is revisited after this research date.

Evidence: ADR-007's source table records the consulted sources and date.
No framework was downloaded or built. Research completion is not owner acceptance.

## 2. Owner decision checkpoint

- [x] Owner accepts, rejects or requests revision of the native-API recommendation; record the explicit outcome and date in ADR-007. **Outcome: accepted as written, 2026-09-18, no revision requested.**
- [x] If accepted, update ADR-007 status and the roadmap consistently while retaining the separate implementation-authorization gate.
- [ ] If JUCE is chosen instead, document the reason, reverify and pin a release, and obtain the owner's licence choice before any dependency introduction. (N/A — native APIs accepted.)
- [x] Confirm that the decision still covers only iOS/iPadOS AUv3 and leaves UI technology undecided.

This checkpoint is complete. ADR-007's `status:` is now `accepted`. Acceptance
selects an architectural direction only — it is not implementation
authorization, and does not itself complete any item in Section 3 below.

## 3. Post-acceptance planning prerequisites

These unchecked items are future documentation/design requirements, not an
instruction to begin them or implementation automatically after acceptance.
Obtain the owner's next bounded scope decision first.

- [x] Design and review the event bridge: one non-render coefficient writer, producer serialization, bounded capacity/work, coalescing/overflow, timestamp/ramp handling, publication latency and deterministic online/offline ordering. Never call existing setters directly from render events or assume callback-thread safety. **Accepted as [ADR-008](../../decisions/ADR-008-parameter-event-bridge.md) (2026-09-18).**
- [x] Preserve ADR-004's block-start consumption and 20 ms internal ramps; if host timing/ramp fidelity demands a change, obtain an explicit ADR revision and bounded-work argument first. ADR-008 (accepted) preserves this unchanged.
- [x] Decide production buses, dry/bypass/tail and buffer aliasing behavior; do not promote the mono-to-stereo DS-B evaluation route into a product contract. **Accepted as [ADR-009](../../decisions/ADR-009-production-bus-policy.md) (2026-09-18): stereo-in/stereo-out with sum-to-mono reduction.**
- [x] Specify state schema/version/migration, invalid-state behavior and restore ordering through the same serialized control boundary; distinguish persistent controls from transient DSP history and stream formats. **Accepted as [ADR-011](../../decisions/ADR-011-state-schema.md) (2026-09-18): three-normalized-double payload plus `schemaVersion` and a fixture stamp; per-field-fallback validation; accept-and-surface fixture-mismatch policy; atomic 3-parameter restore reopening ADR-008's alternative (C) for the StateRestore role only. Names two unimplemented `ParameterAutomation` obligations (read-back accessor, `setAll`).**
- [x] Bound lifecycle/resource/failure handling and the device/OS/sample-rate/block-size matrix; document source-list ownership between portable and Apple builds. **Accepted as [ADR-010](../../decisions/ADR-010-device-lifecycle-matrix.md) (2026-09-18): sample-rate set {48kHz, 44.1kHz}; minimum deployment target iOS/iPadOS 25+ (rolling "current minus one" policy, re-verify before implementation).**
- [x] Define later host/device acceptance cases for lifecycle, state recall, concurrent/dense automation, multiple instances, offline repeatability and realtime allocation/locking/deadline measurements. Record configurations and failures, not a generic compatibility claim. **Accepted as [ADR-012](../../decisions/ADR-012-host-device-acceptance-catalog.md) (2026-09-18): methodology for HT-1…HT-12, plus device/OS scope policy (D). Device/chip-tier scope itself remains owner-open.**
- [ ] Write a separate small wrapper implementation plan with exact files, interfaces, commands, prerequisites, exclusions and rollback boundary. Obtain separate owner implementation authorization before any code/project/dependency/signing work.

## Documentation checks and handoff

Review YAML status, links, source dates/pins, implementation prohibitions and
unmeasured-claim wording. Run `git diff --check` and verify every repository-relative
link in the changed records. The parent
[status reconciliation plan](2026-09-18-status-reconciliation-and-auv3-comparison.md)
owns the Release host configure/build/CTest checkpoint and agent-log handoff.
Passing that checkpoint adds no AUv3 validation evidence.

All six named design prerequisites in Section 3 are now accepted (ADR-008
through ADR-012, plus ADR-004's unchanged ramp preservation). The one
remaining item is a separately authorized bounded wrapper implementation
plan. There is no pre-authorized wrapper, prototype or UI task.
