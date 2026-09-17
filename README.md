# Aetherfield

A greenfield ambient/textural AUv3 audio effect for iOS/iPadOS. The durable product and engineering charter is [AETHERFIELD_SPEC.md](AETHERFIELD_SPEC.md).

**IMPLEMENTED: Phase 0 engineering loop.** Portable C++ gain library, deterministic tests, and an offline PCM WAV renderer. No AUv3 extension or UI exists. **Phase 1 is in progress**: the reverb topology is decided (ADR-002 in [docs/decisions.md](docs/decisions.md)), and all three implementation increments are implemented under separate authorization: S1 (`DelayLine`), S2 (`FeedbackDelayNetwork`: the fixed late network), and parameter transitions (`ParameterAutomation`: Mix/Decay/Damp automation and smoothing over the S2 network, no modulation, no diffusion, no stereo) — see [docs/phase1-s1-plan.md](docs/phase1-s1-plan.md)/[docs/phase1-s2-plan.md](docs/phase1-s2-plan.md)/[docs/phase1-pt-plan.md](docs/phase1-pt-plan.md) and evidence in [docs/testing.md](docs/testing.md). Diffusion, stereo, and every other reverb behavior remain planned, not implemented.

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

See [testing.md](docs/testing.md) for decoded-sample inspection and actual verification evidence.

## Durable project context

- [Architecture](docs/architecture.md): current boundaries and Mermaid diagram.
- [DSP design](docs/dsp-design.md): gain and S1 delay-line contracts, and explicit unimplemented reverb scope beyond them.
- [Decisions](docs/decisions.md): build/framework alternatives and accepted ADR.
- [Roadmap](docs/roadmap.md): Phase 1 scope, agent routing and deferred recommendations.
- [Agent log](docs/agent-log.md): per-milestone tool/role/model/effort provenance and any metrics actually reported. Read this, alongside the roadmap and decisions, before resuming work in a new session or a different tool.

The source layout is deliberately small: `src/dsp/`, `tests/`, and `tools/render/`. Git is local; no remote or distribution license has been selected.
