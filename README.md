# Aetherfield

A greenfield ambient/textural AUv3 audio effect for iOS/iPadOS. The durable product and engineering charter is [AETHERFIELD_SPEC.md](AETHERFIELD_SPEC.md).

**IMPLEMENTED: Phase 0 engineering loop.** Portable C++ gain library, deterministic tests, and an offline PCM WAV renderer. No reverb, AUv3 extension or UI exists. Phase 1 develops the core reverb design and a bounded implementation handoff; planned DSP is not implemented DSP.

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
- [DSP design](docs/dsp-design.md): gain contract and explicit unimplemented reverb scope.
- [Decisions](docs/decisions.md): build/framework alternatives and accepted ADR.
- [Roadmap](docs/roadmap.md): Phase 1 scope, agent routing and deferred recommendations.

The source layout is deliberately small: `src/dsp/`, `tests/`, and `tools/render/`. Git is local; no remote or distribution license has been selected.
