# Roadmap

The charter governs product intent. This roadmap authorizes no work beyond the active milestone. No dates or feature promises are assigned.

## Current continuation — 2026-09-18

S1, S2, parameter automation, DS-A, and the complete DS-B evaluation baseline
are implemented. DS-B Tasks 1–4, DS-1…DS-13, the propagated whole-chain
cessation bound, owner-authorized Mix/Decay/Damp forwarding, and both stereo
render tools are recorded in [testing.md](testing.md); a Release host baseline
passed all **6/6** CTest suites. Three owner listening rounds, DS-13, and Sol
review close the DS-B Sonic acceptance component with no ADR-006 revision
warranted. This closes evidence gathering for the fixed evaluation baseline,
not product authorization.

[ADR-007](decisions/ADR-007-auv3-integration-comparison.md) is **accepted
(2026-09-18)**: native Apple AUv3 APIs are the architectural direction for a
future wrapper, while the portable C++ core stays free of Apple/JUCE types.
Acceptance authorizes no wrapper, UI, Xcode project, dependency, host/device
validation, modulation, or other product implementation — it selects a
framework direction only. The historical milestone sections below retain
their original scope; they are not a substitute for this current status.

All three post-acceptance design prerequisites named in the
[decision-only evaluation plan](superpowers/plans/2026-09-18-auv3-integration-evaluation.md)
are now **accepted**:

- [ADR-008](decisions/ADR-008-parameter-event-bridge.md) — **accepted
  (2026-09-18)**: the nonblocking parameter-event bridge, a per-parameter/
  producer single-slot mailbox design. The owner accepted its named
  coalescing-fidelity risk, deferring the question to a real listening
  test rather than building the higher-fidelity ring-buffer alternative
  up front.
- [ADR-009](decisions/ADR-009-production-bus-policy.md) — **accepted
  (2026-09-18)**: the production bus is stereo-in/stereo-out with a
  sum-to-mono reduction ahead of the existing mono diffusion chain.
  Several other items (bypass CPU-vs-tail tradeoff, kill-tail-on-bypass
  affordance, multi-channel target, `canProcessInPlace`'s actual value
  for the new bus) remain open in ADR-009's "Remaining decisions."
- [ADR-010](decisions/ADR-010-device-lifecycle-matrix.md) — **accepted
  (2026-09-18)**: fixes the sample-rate set to {48kHz, 44.1kHz} and sets
  the minimum deployment target as a rolling "current major version
  minus one" policy (concretely iOS/iPadOS 25+ as of acceptance), chosen
  after a bounded research task found the API-availability floor (iOS
  9.0) placed no real constraint. The concrete number must be
  re-verified against Apple's then-current data before the wrapper
  implementation plan is written, since it is a rolling policy, not a
  fixed one.

Acceptance of ADR-008/009/010 authorizes no wrapper, UI, dependency, or
other implementation — a separately authorized bounded implementation
plan is still required, exactly as ADR-007 required. State schema/
version/migration design and the eventual wrapper implementation plan
itself remain unstarted and are downstream of all three. UI technology
remains deferred; CLAP remains not planned.

## NOW

**Phase 0 — Greenfield bootstrap (IMPLEMENTED; evidence in testing.md).** Prove CODE → BUILD → TEST → RENDER → INSPECT with trivial gain, a portable DSP library, deterministic tests, and a WAV renderer. No reverb, AUv3, UI, presets, or feedback network is implemented.

## NEXT

**Phase 1 — Core Reverb Architecture (architecture and planning rows satisfied; evidence in decisions.md and testing.md).** Topology (ADR-002), decay/damping/numerical-bounds/lifecycle (ADR-003), parameter semantics (ADR-004), the S2 fixture (ADR-005), and the input/output diffusion, injection, tap and stereo strategy (ADR-006) are decided. **S1 (`DelayLine`), S2 (`FeedbackDelayNetwork`, NS-1…NS-11), parameter transitions (`ParameterAutomation`, PT-1…PT-9), DS-A, and the DS-B diffusion/stereo evaluation baseline are implemented and measured; see testing.md.** The product's final `N`, product sample-rate matrix, perceptual `D_max`, AUv3 wrapper, UI, modulation, and product reverb behavior remain deferred or unauthorized. See "Phase 1 exit" below.

Objective: select the smallest coherent architecture capable of a spacious, smooth, slowly evolving ambient field. The charter's FDN proposal is a hypothesis to compare with credible alternatives, not an accepted topology.

| Work | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Compare late-network and diffusion approaches; select topology, feedback matrix and delay strategy | Smooth density and reduced ringing | Phase 0 loop | L | Committing to complexity without audible benefit | Sol | ADR with alternatives, mathematical rationale, and a bounded later prototype/evaluation plan (satisfied by ADR-002; the line count and delay lengths it deferred are fixed for the S2 evaluation fixture by ADR-005, and the diffusion, injection, tap and stereo design it deferred is decided by ADR-006; see decisions.md) |
| Define decay/damping, modulation and numerical bounds, initialization/reset and sample-rate lifecycle | Stable long tails and subtle movement | Topology decision | L | Hidden loop gain or unstable edge combinations | Sol | Explicit stability assumptions and stress-test criteria; no claim of proven sonic quality (satisfied by ADR-003; see decisions.md) |
| Define initial parameter semantics, smoothing and automation boundaries | Playable controls without clicks | Chosen topology | M | Parameter interactions destabilize feedback | Sol | Units/ranges/mapping and transition contracts; candidate controls accepted or deferred with reasons (satisfied by ADR-004; see decisions.md) |
| Translate accepted design into small implementation increments | Reviewable path to a working reverb | Sol decisions | M | Premature abstraction | Terra | Buildable/testable task plan with exact interfaces and ownership; implementation reserved for later authorization (S1 satisfied by phases/phase1-s1-plan.md; the S2 fixed-network task plan, covering the NS-1…NS-11 gate only, is satisfied by phases/phase1-s2-plan.md; the parameter-transitions task plan, covering the PT-1…PT-9 gate only, is satisfied by phases/phase1-pt-plan.md; see decisions.md) |
| Specify deterministic fixtures, metrics and verification cases | Reproducible stability and regression evidence | Design contracts | M | Tests miss perceptual defects | Terra designs; Luna validates | Commands and expected bounds defined; listening review remains required (satisfied by phases/phase1-s2-verification-plan.md, covering the Fixed late network and Parameter transitions gates only) |

Phase 1 exit: architecture and ADRs describe an accepted design, risks, and the smallest subsequent DSP skeleton milestone. No pitch processing, freeze, Bloom, Texture, UI, or multi-engine implementation is implied.

**Phase 1 exit criteria are met.** The accepted design, risks, bounded S1/S2/PT increments, and the DS-B evaluation baseline are documented and evidenced. This authorizes only what has already been built and measured. It does not authorize the remaining product decisions: final `N` and sample-rate matrix, perceptual `D_max`, AUv3/host integration, UI, modulation, or production reverb behavior.

## LATER

| Objective | Benefit | Dependency | Complexity | Primary risk | Agent | Acceptance |
|---|---|---|---|---|---|---|
| Decide and implement future product work in bounded increments | First audible ambient field | Owner scope decision | L | Evaluation evidence mistaken for product readiness | Terra; Sol critical review; Luna QA | Deterministic tests, signal analysis, reference renders and listening evidence; DS-B is closed as an evaluation baseline, while product behavior remains unauthorized |
| Expand corpus and analysis when real decay exists | Detect ringing, stereo and decay regressions | Core DSP | M | Metrics mistaken for listening | Terra; Luna | Repeatable impulse/noise/musical fixtures and justified baselines; never refresh merely to pass |
| Implement iOS/iPadOS AUv3 wrapper and eventual UI now that the framework direction is decided | Playable host integration with appropriate maintenance cost | [ADR-007](decisions/ADR-007-auv3-integration-comparison.md)/[ADR-008](decisions/ADR-008-parameter-event-bridge.md)/[ADR-009](decisions/ADR-009-production-bus-policy.md)/[ADR-010](decisions/ADR-010-device-lifecycle-matrix.md) all accepted; state schema and a separately authorized wrapper plan still needed | L | Lifecycle, automation, signing and CPU constraints | Sol decision; Terra bounded integration; Luna verification | Acceptance of all four is a set of framework/architecture decisions only, not implementation authorization; later work needs actual device/host evidence and measured CPU. UI technology remains undecided |
| Mobile performance and render-thread instrumentation | Reliable audio on supported devices | Nontrivial DSP and Apple integration | M | Host smoke tests overstate realtime safety | Terra; Luna; Sol review | Measured deadlines, allocation checks and documented supported configurations |
| Broaden build verification to another host/toolchain and add CI if useful | Detect portability regressions | Stable portable loop | S | Platform assumptions | Luna | Actual clean builds and tests; platform independence alone is not verified portability |
| Resolve project license, identifiers and name clearance before distribution | Clear distribution terms and identity | Distribution intent | M | Unresolved ownership/naming | Owner; Terra assists | Explicit owner decisions; no license or clearance assumed |

## RESEARCH

**DEFERRED:** Freeze/Infinite, Bloom and Texture only after the core sounds compelling (Sol design, Terra experiments, Luna evidence; L; risk: stability and unnecessary complexity). Acceptance requires isolated audible benefit, stable transitions and documented numerical behavior.

**DEFERRED:** Pitch/spectral processing and advanced modulation require explicit architectural review (Sol; L–XL; dependency: compelling core; risk: loss of sonic coherence). Multi-engine research requires evidence that one network is limiting (Sol; XL). No experiments exist yet.

## NOT PLANNED

**REJECTED for Phase 0:** speculative plugin/UI directories, generic plugin framework layers, production parameter trees, reverb components, preset libraries, CI/dependency machinery without demonstrated need, and copied proprietary algorithms. They do not help prove this milestone's engineering loop.

**NOT PLANNED for initial delivery:** CLAP. It is an additional plugin format, not a replacement for the specified AUv3 target. Reconsider if desktop CLAP becomes an explicit product objective. Other integration frameworks such as iPlug2 remain deferred unless concrete integration requirements justify expanding the native/JUCE comparison.
