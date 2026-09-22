# Aetherfield

An experimental ambient reverb for iOS and iPadOS, built around a portable
C++20 audio-DSP core and a native AUv3 integration path.

Aetherfield is focused on spacious, slowly evolving sound: a fixed late-reverb
network, diffusion, stereo output taps, smooth parameter transitions, and
conservative realtime behavior. The project is being developed as an evidence-
driven engineering prototype, so the repository distinguishes measured
evaluation work from product features that are still deferred.

## What is here

- Portable C++ DSP with a fixed feedback-delay network, damping, decay and Mix
  automation, diffusion, stereo rendering, and wrapper-level fault recovery.
- Deterministic CMake/CTest verification and offline WAV renderers for impulse,
  stereo, listening-batch, and bootstrap-loop experiments.
- A minimal native AUv3 wrapper with parameter bridging, lifecycle handling,
  and a hybrid bypass implementation under active host/device acceptance.
- Documented architecture decisions, measurement contracts, and reproducible
  evidence in [`docs/`](docs/).

## Current status

The portable DSP and stereo evaluation baseline are implemented and measured.
The Release baseline currently passes **8/8 CTest suites**, including the AUv3
wrapper and hybrid-bypass mechanism checks.

The AUv3 path is in acceptance work, not product-release status. Simulator and
physical-device harness evidence exists, while the full host/device acceptance
matrix, commercial-host validation, state restore, UI, modulation, Freeze,
Bloom, Texture, and final product tuning remain open or deferred. See the
[current-state guide](docs/start-here.md) for the exact boundary.

## Build and test

Requirements: CMake 3.25+, a C++20 compiler, and a native build tool. The
portable build has no third-party dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Render a small deterministic bootstrap fixture:

```sh
mkdir -p artifacts
./build/aetherfield_render artifacts/phase0-gain.wav
file artifacts/phase0-gain.wav
```

Render the current diffusion/stereo evaluation path:

```sh
./build/aetherfield_render_diffusion_stereo artifacts/diffusion-stereo.wav
```

Generated build products and render artifacts are ignored by Git. The renderers
produce engineering evidence and listening material; they are not a claim of
finished product sound quality.

## Public showcase

A dual-audience GitHub Pages site lives under [`docs/`](docs/) (`index.html` +
`engineering.html`): hiring skim, listening evidence, and engineering proof
with honest shipped-vs-deferred boundaries.

- Local: open [`docs/index.html`](docs/index.html)
- Public URL (after Pages is enabled from `/docs`):
  https://joshband.github.io/aetherfield/

Enable with: GitHub → Settings → Pages → Deploy from branch → `/docs`.

## Read next

- [Current state and task routing](docs/start-here.md)
- [Verification evidence](docs/testing.md)
- [Architecture](docs/architecture.md)
- [Decision records](docs/decisions/index.md)
- [Roadmap and authorization boundaries](docs/roadmap.md)
- [Product charter](AETHERFIELD_SPEC.md)

The repository is an active engineering prototype. Product scope, licensing,
identifiers, and distribution decisions have not been finalized.
