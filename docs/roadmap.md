# Roadmap

The charter governs product intent. This roadmap authorizes no work beyond the active milestone. No dates or feature promises are assigned.

## Current continuation — 2026-09-17

S1, S2, parameter automation, DS-A, DS-B Task 1 lifecycle/preparation, the
DS-B Task 2a read-only FDN pre-step tap accessor, and DS-B Task 2b's
one-sample mono-to-stereo route and DS-B Task 3's aggregate detector/block
recovery are implemented; a fresh Release build and all six CTest suites passed
during the Codex handoff. ADR-006 remains an evaluation baseline; its
integration acceptance claims are
corrected by contract in the [implementation-readiness review](phases/phase1-ds-review.md)
and bounded in the [DS-B integration plan](phases/phase1-ds-integration-plan.md).
The [DS-A allpass primitive plan](phases/phase1-ds-plan.md) is now **IMPLEMENTED**
as a standalone primitive; its focused and full-suite evidence is in
testing.md. It does not implement full diffusion/stereo integration. That
integration requires separate authorization for DS-B Tasks 3–4, then
measurements of decay, coherence, silence, detector behavior and channel
metrics. The historical milestone sections below retain their original scope;
they are not a substitute for this current status.

## NOW

**Phase 0 — Greenfield bootstrap (IMPLEMENTED; evidence in testing.md).** Prove CODE → BUILD → TEST → RENDER → INSPECT with trivial gain, a portable DSP library, deterministic tests, and a WAV renderer. No reverb, AUv3, UI, presets, or feedback network is implemented.

## NEXT

**Phase 1 — Core Reverb Architecture (architecture and planning rows satisfied; evidence in decisions.md and testing.md).** Topology (ADR-002), decay/damping/numerical-bounds/lifecycle (ADR-003), and initial parameter semantics (ADR-004) are decided, the S2 evaluation fixture's line count, delay-length set and sample rates (ADR-005) are fixed, and the input/output diffusion topology, injection vector, output tap design and stereo decorrelation strategy (ADR-006) are decided — the product's final `N` remains DEFERRED to S2's measured evidence; the S1 skeleton (phases/phase1-s1-plan.md) and the consolidated S2/parameter verification-case specification (phases/phase1-s2-verification-plan.md) are planned. All five table rows below are satisfied. **S1 (`DelayLine`), S2 (`FeedbackDelayNetwork`: the fixed late network, NS-1…NS-11) and parameter transitions (`ParameterAutomation`: Mix/Decay/Damp, PT-1…PT-9) are all implemented and independently verified; see testing.md.** No diffusion, stereo strategy, or other reverb behavior beyond the automated fixed network is implemented. See "Phase 1 exit" below.

Objective: select the smallest coherent architecture capable of a spacious, smooth, slowly evolving ambient field. The charter's FDN proposal is a hypothesis to compare with credible alternatives, not an accepted topology.

| Work | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Compare late-network and diffusion approaches; select topology, feedback matrix and delay strategy | Smooth density and reduced ringing | Phase 0 loop | L | Committing to complexity without audible benefit | Sol | ADR with alternatives, mathematical rationale, and a bounded later prototype/evaluation plan (satisfied by ADR-002; the line count and delay lengths it deferred are fixed for the S2 evaluation fixture by ADR-005, and the diffusion, injection, tap and stereo design it deferred is decided by ADR-006; see decisions.md) |
| Define decay/damping, modulation and numerical bounds, initialization/reset and sample-rate lifecycle | Stable long tails and subtle movement | Topology decision | L | Hidden loop gain or unstable edge combinations | Sol | Explicit stability assumptions and stress-test criteria; no claim of proven sonic quality (satisfied by ADR-003; see decisions.md) |
| Define initial parameter semantics, smoothing and automation boundaries | Playable controls without clicks | Chosen topology | M | Parameter interactions destabilize feedback | Sol | Units/ranges/mapping and transition contracts; candidate controls accepted or deferred with reasons (satisfied by ADR-004; see decisions.md) |
| Translate accepted design into small implementation increments | Reviewable path to a working reverb | Sol decisions | M | Premature abstraction | Terra | Buildable/testable task plan with exact interfaces and ownership; implementation reserved for later authorization (S1 satisfied by phases/phase1-s1-plan.md; the S2 fixed-network task plan, covering the NS-1…NS-11 gate only, is satisfied by phases/phase1-s2-plan.md; the parameter-transitions task plan, covering the PT-1…PT-9 gate only, is satisfied by phases/phase1-pt-plan.md; see decisions.md) |
| Specify deterministic fixtures, metrics and verification cases | Reproducible stability and regression evidence | Design contracts | M | Tests miss perceptual defects | Terra designs; Luna validates | Commands and expected bounds defined; listening review remains required (satisfied by phases/phase1-s2-verification-plan.md, covering the Fixed late network and Parameter transitions gates only) |

Phase 1 exit: architecture and ADRs describe an accepted design, risks, and the smallest subsequent DSP skeleton milestone. No pitch processing, freeze, Bloom, Texture, UI, or multi-engine implementation is implied.

**Phase 1 exit criteria are met as of ADR-002/003/004 and phases/phase1-s1-plan.md/phases/phase1-s2-verification-plan.md.** The accepted design, its risks, and the smallest subsequent DSP skeleton milestone are all documented. The owner separately authorized implementing S1, then S2, then parameter transitions; all three are now implemented and independently verified (testing.md). This authorizes only what was implemented — the fixed late network's NS-1…NS-11 gate and its PT-1…PT-9 automation layer — not the rest of the LATER table's first row: core reverb behavior under real product usage (diffusion, stereo, the product's final `N` and sample-rate matrix, `D_max`'s actual perceptual value) still requires its own explicit authorization, per every ADR's "authorizes no implementation" statement.

## LATER

| Objective | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Implement the accepted DSP skeleton, then core reverb in bounded increments | First audible ambient field | Phase 1 approval | L | Stable math fails perceptually | Terra; Sol critical review; Luna QA | Deterministic tests, signal analysis, reference renders and listening evidence (S1, S2 and parameter transitions satisfied; see testing.md. Diffusion, stereo, and core reverb behavior under real product usage remain unauthorized) |
| Expand corpus and analysis when real decay exists | Detect ringing, stereo and decay regressions | Core DSP | M | Metrics mistaken for listening | Terra; Luna | Repeatable impulse/noise/musical fixtures and justified baselines; never refresh merely to pass |
| Decide native Apple APIs versus JUCE, then implement iOS/iPadOS AUv3 wrapper and eventual UI | Playable host integration with appropriate maintenance cost | Proven DSP and parameter contracts | L | Lifecycle, automation, signing and CPU constraints | Sol decision; Terra bounded integration comparison/implementation; Luna verification | Integration ADR comparing lifecycle, automation/state, UI, device/host validation, build complexity, licensing and actual format needs; then device/host evidence and measured CPU |
| Mobile performance and render-thread instrumentation | Reliable audio on supported devices | Nontrivial DSP and Apple integration | M | Host smoke tests overstate realtime safety | Terra; Luna; Sol review | Measured deadlines, allocation checks and documented supported configurations |
| Broaden build verification to another host/toolchain and add CI if useful | Detect portability regressions | Stable portable loop | S | Platform assumptions | Luna | Actual clean builds and tests; platform independence alone is not verified portability |
| Resolve project license, identifiers and name clearance before distribution | Clear distribution terms and identity | Distribution intent | M | Unresolved ownership/naming | Owner; Terra assists | Explicit owner decisions; no license or clearance assumed |

## RESEARCH

**DEFERRED:** Freeze/Infinite, Bloom and Texture only after the core sounds compelling (Sol design, Terra experiments, Luna evidence; L; risk: stability and unnecessary complexity). Acceptance requires isolated audible benefit, stable transitions and documented numerical behavior.

**DEFERRED:** Pitch/spectral processing and advanced modulation require explicit architectural review (Sol; L–XL; dependency: compelling core; risk: loss of sonic coherence). Multi-engine research requires evidence that one network is limiting (Sol; XL). No experiments exist yet.

## NOT PLANNED

**REJECTED for Phase 0:** speculative plugin/UI directories, generic plugin framework layers, production parameter trees, reverb components, preset libraries, CI/dependency machinery without demonstrated need, and copied proprietary algorithms. They do not help prove this milestone's engineering loop.

**NOT PLANNED for initial delivery:** CLAP. It is an additional plugin format, not a replacement for the specified AUv3 target. Reconsider if desktop CLAP becomes an explicit product objective. Other integration frameworks such as iPlug2 remain deferred unless concrete integration requirements justify expanding the native/JUCE comparison.
