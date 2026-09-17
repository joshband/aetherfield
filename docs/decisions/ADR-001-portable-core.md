---
id: "ADR-001"
status: accepted
implementation: "bootstrap-implemented; auv3-integration-deferred"
review: current
review_document: null
depends_on: []
---

# ADR-001 — Portable C++ host loop; AUv3 framework decision deferred

## Review summary

- **Decision:** keep the Phase 0 loop in portable C++20/CMake.
- **Why:** it proves build, test, and offline rendering without committing to an AUv3 framework.
- **Consequence:** the gain-only bootstrap is implemented; AUv3 integration is deferred.
- **Uncertainty:** native Apple APIs versus JUCE needs an integration-focused ADR and host evidence.

<!-- Original ADR record -->

## ADR-001 — Portable C++ host loop; AUv3 framework decision deferred

**Status:** Accepted

### Context

Aetherfield begins greenfield as an iOS/iPadOS AUv3 audio effect. Phase 0 must prove CODE → BUILD → TEST → RENDER → INSPECT without implementing reverb or requiring an Audio Unit host. Sol reviewed this consequential boundary; Astra accepted it. Terra evaluated feasibility and implements the approved bootstrap; Luna independently verifies it.

### Alternatives

These are project-specific engineering judgments, not claims that alternative tools cannot work.

| Criterion | CMake core + future native Xcode | Xcode-only | Swift Package Manager + Xcode | JUCE + CMake/Xcode |
|---|---|---|---|---|
| AUv3 support | Later native extension; none today | Native extension targets | Still needs extension/app packaging | Framework supports AUv3 |
| iOS/iPadOS development | Later native Apple workflow | Direct Apple workflow | Good Swift integration; still Apple project work | Supported mobile framework workflow |
| C++ DSP isolation | Explicit independent library | Possible, but Apple project owns host loop | Possible C++ target; interop/module boundary | Possible separate core; wrapper adds framework |
| Offline testability | Small CLI targets and CTest | Possible command-line test targets | Possible executable/test targets | Good, but no framework needed for this loop |
| Build reproducibility | Text build graph; explicit toolchain; no downloads | Schemes/project/SDK settings need control | Package/toolchain control plus Apple packaging | Framework version/license and generator require control |
| Agentic development | Small reviewable files and direct CLI commands | More project-file maintenance early | Simple manifest, additional mixed-language constraints | Broad framework API and configuration surface |
| Dependency burden | CMake, compiler, build tool; standard C++ library | Apple tooling | Swift tooling and later Xcode | Additional third-party framework and licensing choice |
| Maintainability | Small current graph; later cross-project drift risk | One Apple-centric graph; less portable host workflow | Attractive if Swift app boundary benefits later | Attractive if wider platform/format requirements justify it |

### Decision

Use C++20 and CMake for a portable static DSP library, a CTest-registered test executable and an offline WAV renderer. Require no third-party DSP, test or audio-file library. Use a concrete stateless gain function as the only current processor.

Plan an AUv3 integration layer around the same core when integration is authorized. Its framework remains **DEFERRED**: native Apple APIs and JUCE are the serious shortlist, to be decided in an integration-focused ADR. Do not generate an Apple project now. CMake-only AUv3 packaging is not selected. Swift Package Manager, JUCE and Xcode-only are **REJECTED for this bootstrap**, not prohibited forever. UI technology and production parameter transport remain **DEFERRED**.

Following the owner's JUCE/CLAP question, Sol reviewed the distinction explicitly. [CLAP](https://github.com/free-audio/clap) defines a host/plugin ABI and extensions; it is a plugin format, not an alternative AUv3 framework. CLAP is **NOT PLANNED** for the initial delivery; a later desktop CLAP requirement would justify reconsideration. [iPlug2](https://github.com/iPlug2/iPlug2) is another credible plugin framework, but a deeper evaluation is deferred until integration needs justify expanding the shortlist.

### Rationale

The current task needs a portable compiler loop, not a plugin framework. Standard C++ with an explicit library target keeps allocation and ownership visible and supports offline experimentation. Either native Apple APIs or JUCE can later wrap the core without becoming a DSP dependency.

Native integration offers direct lifecycle control and fewer dependencies, but requires more Apple-specific glue. JUCE can reduce wrapper, parameter/state and UI work and help if other formats become requirements; it introduces framework conventions, licensing decisions and a larger build surface. Neither is proven preferable for the eventual product by this trivial host loop.

This separates host-loop proof from future deployment proof. CMake describes the build reproducibly, but does not guarantee bit-identical binaries across compilers or environments.

### Evidence

- [Apple AUAudioUnit documentation](https://developer.apple.com/documentation/AudioToolbox/AUAudioUnit) and [Audio Unit extension guide](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html) describe the native extension and render lifecycle.
- [CMake toolchains manual](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html) documents explicit toolchains and Apple cross-compilation. The current build validates only the host targets.
- [Swift C++ interoperability](https://www.swift.org/documentation/cxx-interop/) documents C++/Swift package and interoperability configuration; Phase 0 needs neither Swift nor a bridging layer.
- [JUCE repository](https://github.com/juce-framework/JUCE) and [CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) establish AUv3 support and its Xcode generator requirement. Framework capabilities exceed this milestone's needs.
- Local environment: CMake 4.2.2; Apple Command Line Tools clang 21.0.0, arm64. `DEVELOPER_DIR=/Library/Developer/CommandLineTools` selects a working host compiler without changing system settings. The globally selected Xcode installation reports an unaccepted license; native iOS validation is not claimed.
- Actual acceptance commands and results are recorded in [testing.md](../testing.md).

### Consequences

DSP remains Apple-independent; tests/rendering work without an AUv3 host. There is no smoothing, state serialization, host parameter tree or concurrent parameter transport. A fixed scalar gain is only a bootstrap fixture input.

Later integration must prevent CMake/Xcode source and build-setting drift. C++20 compatibility, deployment target, architecture slices, device CPU budget, signing and real host behavior require actual validation when Apple integration starts. No license or product identifiers are invented now.

### Revisit When

Native AUv3 integration begins, another platform/format is approved, shared-source maintenance becomes brittle, or evidence shows SPM/JUCE reduces total integration cost enough to justify its constraints.
