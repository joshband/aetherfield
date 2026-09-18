# AUv3 integration decision-only evaluation plan

**Status:** research drafted; owner decision pending. **No implementation authorized.**

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

- [ ] Owner accepts, rejects or requests revision of the native-API recommendation; record the explicit outcome and date in ADR-007.
- [ ] If accepted, update ADR-007 status and the roadmap consistently while retaining the separate implementation-authorization gate.
- [ ] If JUCE is chosen instead, document the reason, reverify and pin a release, and obtain the owner's licence choice before any dependency introduction.
- [ ] Confirm that the decision still covers only iOS/iPadOS AUv3 and leaves UI technology undecided.

Until this checkpoint is complete, ADR-007 remains `status: proposed`.
No elapsed time, successful host test run or agent review substitutes for the
owner's acceptance.

## 3. Post-acceptance planning prerequisites

These unchecked items are future documentation/design requirements, not an
instruction to begin them or implementation automatically after acceptance.
Obtain the owner's next bounded scope decision first.

- [ ] Design and review the event bridge: one non-render coefficient writer, producer serialization, bounded capacity/work, coalescing/overflow, timestamp/ramp handling, publication latency and deterministic online/offline ordering. Never call existing setters directly from render events or assume callback-thread safety.
- [ ] Preserve ADR-004's block-start consumption and 20 ms internal ramps; if host timing/ramp fidelity demands a change, obtain an explicit ADR revision and bounded-work argument first.
- [ ] Decide production buses, dry/bypass/tail and buffer aliasing behavior; do not promote the mono-to-stereo DS-B evaluation route into a product contract.
- [ ] Specify state schema/version/migration, invalid-state behavior and restore ordering through the same serialized control boundary; distinguish persistent controls from transient DSP history and stream formats.
- [ ] Bound lifecycle/resource/failure handling and the device/OS/sample-rate/block-size matrix; document source-list ownership between portable and Apple builds.
- [ ] Define later host/device acceptance cases for lifecycle, state recall, concurrent/dense automation, multiple instances, offline repeatability and realtime allocation/locking/deadline measurements. Record configurations and failures, not a generic compatibility claim.
- [ ] Write a separate small wrapper implementation plan with exact files, interfaces, commands, prerequisites, exclusions and rollback boundary. Obtain separate owner implementation authorization before any code/project/dependency/signing work.

## Documentation checks and handoff

Review YAML status, links, source dates/pins, implementation prohibitions and
unmeasured-claim wording. Run `git diff --check` and verify every repository-relative
link in the changed records. The parent
[status reconciliation plan](2026-09-18-status-reconciliation-and-auv3-comparison.md)
owns the Release host configure/build/CTest checkpoint and agent-log handoff.
Passing that checkpoint adds no AUv3 validation evidence.

The next decision is owner acceptance of ADR-007 after review. There is no
pre-authorized wrapper, prototype or UI task.
