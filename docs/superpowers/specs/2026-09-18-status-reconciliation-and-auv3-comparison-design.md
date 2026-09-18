# Status Reconciliation and AUv3 Comparison Design

## Purpose

Align maintained status surfaces with the committed DS-B completion record, then
start the deferred native-Apple-versus-JUCE decision as documentation-only
research. This authorizes no product implementation.

## Facts to propagate

- DS-B Tasks 1 through 4 are implemented. DS-1 through DS-13 are recorded in
  `docs/testing.md`, including the propagated cessation bound and per-channel
  magnitude response.
- DS-B Sonic acceptance is closed after three owner listening rounds, DS-13,
  and Sol review; no ADR-006 revision is warranted.
- `DiffusionStereoPath` provides owner-authorized Mix, Decay, and Damp
  forwarding, and both stereo render tools are implemented.
- The current host Release baseline is six passing CTest suites. It is not
  iOS, AUv3-host, device, realtime, or product-sonic certification.
- This closure authorizes no product signal path, host wrapper, UI,
  modulation, Freeze, Bloom, Texture, or final-product-line-count work.

`docs/start-here.md`, `docs/testing.md`, the closed DS-B plan, and source/tests
establish these facts. Clearly dated historical accounts remain historical.

## Documentation boundary

Reconcile current-status claims in `README.md`, `docs/roadmap.md`,
`docs/architecture.md`, `docs/dsp-design.md`, ADR-006 and its index/plan/review,
`docs/testing.md`, and `docs/site/index.html`. Inspect `AETHERFIELD_SPEC.md` as
the product charter; change it only for a demonstrably stale present-state
assertion. Do not turn product intent or future diagrams into status text.

The roadmap must name the AUv3 comparison as the active decision-only
continuation while prohibiting wrapper/UI implementation. The documentation site
must agree with the Markdown sources.

## AUv3 comparison deliverables

Create canonical `ADR-007` with `status: proposed`, not accepted. Compare native
Apple Audio Unit APIs and JUCE using official primary sources for current claims.
Cover lifecycle, render/parameter/state ownership, UI, build/signing,
licensing/dependencies, host/device validation, realtime/CPU instrumentation,
and format needs. Any recommendation remains pending owner acceptance.

Create a decision-only evaluation plan. It prohibits Xcode projects, JUCE
fetches/dependencies, wrappers, UI, build-target changes, spikes, and CLAP work.

## Verification and non-goals

Run the Release configure/build/CTest loop, stale-phrase/link searches, and
`git diff --check`. Confirm the proposed ADR does not claim acceptance or
implementation. No source, test, CMake, tool, dependency, project file, wrapper,
UI, signing, publication, or feature work is in scope.
