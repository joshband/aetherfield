# Aetherfield

A greenfield ambient/textural AUv3 audio effect for iOS/iPadOS. The durable product and engineering charter is [AETHERFIELD_SPEC.md](AETHERFIELD_SPEC.md).

**Start here:** [Current state and task-specific reading guide](docs/start-here.md).
For decisions, use the [ADR index](docs/decisions/index.md); each record has a
short review summary, machine-readable metadata, and its complete original reasoning.

**Implemented and measured:** the portable C++ host loop, `DelayLine`, fixed
`FeedbackDelayNetwork`, Mix/Decay/Damp `ParameterAutomation`,
`SchroederAllpass`, and the ADR-006 `DiffusionStereoPath` evaluation baseline
(input/output diffusion, normalized FDN injection, stereo taps, Mix, aggregate
fault recovery, and owner-authorized control forwarding). Six host CTest suites
cover these increments. DS-B Tasks 1–4 and DS-1…DS-13 are closed; the DS-B
Sonic acceptance component is closed after three owner listening rounds,
DS-13, and Sol review. This is evidence for the fixed evaluation baseline, not
authorization for a product signal path, AUv3 wrapper, UI, modulation, Freeze,
Bloom, Texture, or a final product line count. AUv3 and UI are deferred.
See the current-state guide for evidence and the owner-gated next decision.

## Build, test, render, inspect

Requires CMake 3.25+, a C++20 compiler and a native build tool. No third-party dependencies are downloaded. Run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
mkdir -p artifacts
./build/aetherfield_render artifacts/phase0-gain.wav
file artifacts/phase0-gain.wav
```

The bootstrap host loop uses the separately installed Command Line Tools, as requested. Select them in the current shell:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
```

The Xcode license issue encountered in Phase 0 is resolved: Xcode 27.0 (27A266a) was verified on 2026-09-16. Selecting Command Line Tools does not change the system's Xcode selection or validate iOS builds. Other hosts with a configured compiler should omit it. The commands above use a single-configuration build; multi-configuration generators require their corresponding configuration options and executable paths.

The renderer writes a fixed 16-frame, 48 kHz, mono, signed 16-bit PCM fixture at gain 0.5. It is a structural/sample correctness artifact, not a musical audition. It requires an existing output directory and replaces the named output file. Build products and generated artifacts are ignored by Git.

A second tool, `aetherfield_render_reverb`, renders a single deterministic impulse response through the now-implemented `FeedbackDelayNetwork`/`ParameterAutomation` pipeline, for a first observational listen (testing.md's Sonic acceptance gate):

```sh
./build/aetherfield_render_reverb artifacts/s2-pt-impulse.wav
```

This is explicitly an **S2/PT observation, not an acceptance** (ADR-002): it
predates the implemented DS-B evaluation path, and its fixture values are
stated and non-tuned, not product defaults. DS-B's separate Sonic acceptance
component is closed in [testing.md](docs/testing.md); neither result authorizes
a product signal path.

See [testing.md](docs/testing.md) for decoded-sample inspection and actual verification evidence.

## Durable project context

- [Architecture](docs/architecture.md): current boundaries and Mermaid diagram.
- [DSP design](docs/dsp-design.md): implemented gain, delay, fixed network,
  automation, and DS-B evaluation-path contracts, with product scope deferred.
- [Decision index](docs/decisions/index.md): individual ADRs, status, dependencies, and implementation/review state. [Legacy links](docs/decisions.md) remain available.
- [Roadmap](docs/roadmap.md): Phase 1 scope, agent routing and deferred recommendations.
- [Agent log](docs/agent-log.md): per-milestone tool/role/model/effort provenance and measured metrics. Consult it when attribution or historical verification matters.
- [Field Notes site](docs/site/index.html): a visual summary of the above (status, architecture, ADRs, roadmap, testing evidence, agent log) as a single local HTML file — open it directly in a browser. It is the canonical source for the published copy at https://claude.ai/artifact/KzdPDSvRqQCKJYysPuHnkT; edit the local file and republish to that same URL, never the reverse. One caveat: the two Mermaid diagrams on the Architecture page only render on the published copy (they depend on a runtime the hosting platform injects) — opened as a local file they show as plain text, which is expected, not a bug.

The source layout is deliberately small: `src/dsp/`, `tests/`, `tools/render/`, `tools/render_reverb/`, and `docs/site/` (the documentation website's canonical source). Git is local; no remote or distribution license has been selected.
