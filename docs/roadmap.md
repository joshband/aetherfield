# Roadmap

The charter governs product intent. This roadmap authorizes no work beyond the active milestone. No dates or feature promises are assigned.

## NOW

**Phase 0 — Greenfield bootstrap (IMPLEMENTED; evidence in testing.md).** Prove CODE → BUILD → TEST → RENDER → INSPECT with trivial gain, a portable DSP library, deterministic tests, and a WAV renderer. No reverb, AUv3, UI, presets, or feedback network is implemented.

## NEXT

**Phase 1 — Core Reverb Architecture (architecture and planning rows satisfied; evidence in decisions.md and testing.md).** Topology (ADR-002), decay/damping/numerical-bounds/lifecycle (ADR-003), and initial parameter semantics (ADR-004) are decided, and the S2 evaluation fixture's line count, delay-length set and sample rates (ADR-005) are fixed — the product's final `N` remains DEFERRED to S2's measured evidence; the S1 skeleton (phase1-s1-plan.md) and the consolidated S2/parameter verification-case specification (phase1-s2-verification-plan.md) are planned. All five table rows below are satisfied. **S1 (`DelayLine`: a single fixed-length delay line, no feedback/matrix/damping) is implemented and independently verified; see testing.md.** No feedback network, decay, or other reverb behavior is implemented — S1 is a lifecycle/primitive skeleton, not a reverb. See "Phase 1 exit" below.

Objective: select the smallest coherent architecture capable of a spacious, smooth, slowly evolving ambient field. The charter's FDN proposal is a hypothesis to compare with credible alternatives, not an accepted topology.

| Work | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Compare late-network and diffusion approaches; select topology, feedback matrix and delay strategy | Smooth density and reduced ringing | Phase 0 loop | L | Committing to complexity without audible benefit | Sol | ADR with alternatives, mathematical rationale, and a bounded later prototype/evaluation plan (satisfied by ADR-002; the line count and delay lengths it deferred are fixed for the S2 evaluation fixture by ADR-005; see decisions.md) |
| Define decay/damping, modulation and numerical bounds, initialization/reset and sample-rate lifecycle | Stable long tails and subtle movement | Topology decision | L | Hidden loop gain or unstable edge combinations | Sol | Explicit stability assumptions and stress-test criteria; no claim of proven sonic quality (satisfied by ADR-003; see decisions.md) |
| Define initial parameter semantics, smoothing and automation boundaries | Playable controls without clicks | Chosen topology | M | Parameter interactions destabilize feedback | Sol | Units/ranges/mapping and transition contracts; candidate controls accepted or deferred with reasons (satisfied by ADR-004; see decisions.md) |
| Translate accepted design into small implementation increments | Reviewable path to a working reverb | Sol decisions | M | Premature abstraction | Terra | Buildable/testable task plan with exact interfaces and ownership; implementation reserved for later authorization (S1 satisfied by phase1-s1-plan.md; see decisions.md) |
| Specify deterministic fixtures, metrics and verification cases | Reproducible stability and regression evidence | Design contracts | M | Tests miss perceptual defects | Terra designs; Luna validates | Commands and expected bounds defined; listening review remains required (satisfied by phase1-s2-verification-plan.md, covering the Fixed late network and Parameter transitions gates only) |

Phase 1 exit: architecture and ADRs describe an accepted design, risks, and the smallest subsequent DSP skeleton milestone. No pitch processing, freeze, Bloom, Texture, UI, or multi-engine implementation is implied.

**Phase 1 exit criteria are met as of ADR-002/003/004 and phase1-s1-plan.md/phase1-s2-verification-plan.md.** The accepted design, its risks, and the smallest subsequent DSP skeleton milestone are all documented. The owner separately authorized implementing that S1 skeleton; it is now implemented and independently verified (testing.md). This authorized S1 alone, not the rest of the LATER table's first row: the fixed late network (ADR-002's S2), core reverb behavior, and everything after it still require their own explicit authorization, per every ADR's "authorizes no implementation" statement.

## LATER

| Objective | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Implement the accepted DSP skeleton, then core reverb in bounded increments | First audible ambient field | Phase 1 approval | L | Stable math fails perceptually | Terra; Sol critical review; Luna QA | Deterministic tests, signal analysis, reference renders and listening evidence (S1 skeleton satisfied; see testing.md. S2 fixed late network and beyond remain unauthorized) |
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
