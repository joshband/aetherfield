---
id: "ADR-007"
status: accepted
accepted: "2026-09-18"
implementation: "none; architectural direction only, no wrapper/UI/dependency code"
review: "owner-accepted-2026-09-18"
review_document: "../superpowers/plans/2026-09-18-auv3-integration-evaluation.md"
depends_on: [ADR-001, ADR-004, ADR-006]
---

# ADR-007 — Native Apple APIs versus JUCE for iOS/iPadOS AUv3

## Review summary

- **Decision:** use native Apple AUv3 APIs around the portable C++ DSP core for the current single-format iOS/iPadOS scope.
- **Why:** direct ownership of the Apple boundary fits the present target; JUCE's wider platform/format support does not yet answer an approved requirement.
- **Consequence:** the team would own lifecycle, parameter bridging, state and packaging glue. This is an engineering judgment, not a measured cost or performance advantage.
- **Uncertainty:** the event bridge, production buses, state policy, Apple build/signing and device/host evidence remain open. UI technology remains deferred.

**Status: Accepted (2026-09-18) — architectural direction only.** The owner accepted the native-API recommendation as written, with no revision. This record authorizes no wrapper, UI, dependency, or other implementation. It does not supersede [ADR-001](ADR-001-portable-core.md)'s deferred integration boundary. A separately authorized bounded implementation plan is still required before any code, and the parameter-event bridge and production bus/state design (below) remain open prerequisites to that plan.

## Context and scope

The [roadmap](../roadmap.md) now permits this documentation-only comparison.
The DS-B evaluation baseline and Sonic acceptance component are closed;
[testing.md](../testing.md) records DS-1…DS-13 and six passing host CTest suites.
Those results establish neither an AUv3 product nor iOS/device/host behavior.
The existing mono-input/stereo-output evaluation route is not a production
AU bus contract. Final product configuration, supported rates and perceptual
limits are not chosen by this framework decision.

Both alternatives must preserve the independently buildable C++20/CMake core
and offline tests. No Apple or JUCE types belong in that core. CLAP remains
not planned; other formats and frameworks require a new scope decision.
No wrapper, UI, Xcode project, signing configuration, identifier, dependency,
build target or spike is created by this comparison.

## Evidence basis

Official sources were consulted on 2026-09-18. Apple API documentation establishes
platform contracts; the archived extension guide establishes packaging context,
not current deployment-version eligibility. JUCE **8.0.12** is a pinned
comparison specimen, not a selected Aetherfield dependency or a claim that it
is the latest release. Any later JUCE choice needs a fresh exact pin and an
owner-approved licence decision.

| Source | What it supports |
|---|---|
| [Apple AUAudioUnit](https://developer.apple.com/documentation/audiotoolbox/auaudiounit) | Native subclass/factory boundary, buses, resource lifecycle, rendering and parameter tree. |
| [Apple AUInternalRenderBlock](https://developer.apple.com/documentation/audiotoolbox/auinternalrenderblock) and [AUParameterEvent](https://developer.apple.com/documentation/audiotoolbox/auparameterevent) | Render events carry timing and ramp information; buffer lifetime and input-pull responsibilities are explicit. |
| [Apple fullState](https://developer.apple.com/documentation/audiotoolbox/auaudiounit/fullstate?language=objc) | Persistable state and base-class support for saving parameter-tree values. |
| [Apple Audio Unit extension guide](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html) | Audio Unit extension, containing app and custom-view architecture. |
| [JUCE 8.0.12 CMake API](https://github.com/juce-framework/JUCE/blob/8.0.12/docs/CMake%20API.md) | AUv3 format support, Xcode generator requirement, iOS configuration/signing and CMake targets. |
| [JUCE 8.0.12 AudioProcessor header](https://github.com/juce-framework/JUCE/blob/8.0.12/modules/juce_audio_processors_headless/processors/juce_AudioProcessor.h) | Processor preparation/release, block processing, bus negotiation and optional editor boundary. |
| [JUCE 8.0.12 licence](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md) and [official JUCE 8 terms](https://juce.com/legal/juce-8-licence/) | AGPLv3/commercial licensing alternatives and dependency notices; no licence is selected here. |

These are API/documentation findings, not results from building either wrapper.
No binary size, CPU, development-speed or host-reliability comparison has been
measured. No source is evidence that Aetherfield works in an AUv3 host.

## Alternatives and ownership

References in the evidence table support API facts below; relative maintenance
costs are project-specific judgments.

| Concern | Native Apple APIs | JUCE | Aetherfield obligation under either route |
|---|---|---|---|
| Lifecycle | Own an `AUAudioUnit` subclass/factory and resource allocation, deallocation and reset mapping. | Adapt the core through `AudioProcessor` preparation/release and processing callbacks; framework supplies the format layer. | Specify preparation failure, reconfiguration, reset/tail and instance ownership; test resource cycles. |
| Render and buses | Own input pulls, buffer mapping/lifetime and render-event intake. | Use framework block/bus interfaces; variable and zero-length callbacks remain relevant. | Decide production bus layouts and buffer aliasing policy before wrapping; retain bounded render work. DS-B's mono-to-stereo route does not decide these. |
| Parameters | Use a parameter tree and AU render events at the Apple boundary. | Use framework parameter facilities at the format boundary. | Neither route makes the current setters safe on an audio thread. Resolve the bridge below first. |
| State | `fullState` already supplies default parameter-tree persistence; custom properties may require extension. | Framework integration still needs a product-defined state payload and compatibility policy. | Define schema/version/migration, validation and restore ordering. Neither API choice decides what a preset means across fixture changes. |
| UI | A future custom AU view can live at the platform boundary. | Optional processor editor provides a framework UI boundary. | Defer UI implementation and toolkit choice; native does not select SwiftUI or UIKit. |
| Build and signing | Own Apple app/extension packaging and reconcile its source list/settings with the portable build. | CMake support can retain a separate C++ core; AUv3 generation requires Xcode, with iOS signing still explicit. | Control source/build-setting drift, SDK/deployment matrix and reproducible commands; identifiers, entitlements and signing remain future decisions. |
| Dependencies and licence | Adds no JUCE framework dependency; Apple tooling and distribution obligations remain. | Adds framework version/module maintenance and AGPLv3 versus commercial licence selection. | Owner resolves project/distribution licensing; no cost tier, eligibility or legal compatibility is assumed. |
| Validation | Native contracts are directly visible, with project-owned glue to verify. | Framework support supplies useful integration machinery, but does not verify this product. | Later test actual devices/hosts, state round trips, parameter traffic, offline rendering, lifecycle and multiple instances. |
| Realtime instrumentation | Instrument the native entry point and core boundary. | Instrument the framework entry path and core boundary. | Later measure callback deadlines, allocation/locking behavior and stress failures on named devices; desktop cost evidence is insufficient. |
| Format needs | Directly addresses the one approved AUv3/iOS/iPadOS target. | Broader platform/format support could become valuable if scope expands. | Preserve AUv3-only scope. No CLAP work or speculative abstraction. |

## Required parameter bridge decision

[ParameterAutomation](../../src/dsp/ParameterAutomation.h)'s setters validate
controls and derive coefficients off the render thread; they are explicitly
**not realtime safe and single-writer**. The forwarding setters on
[DiffusionStereoPath](../../src/dsp/DiffusionStereoPath.h) inherit that contract.
The existing atomic coefficient publication is not an AU event adapter.
Do not wire AUParameterTree callbacks, AU render events or JUCE parameter
callbacks directly to these setters: callback context and producer concurrency
must first be accounted for.

Before any wrapper implementation plan, a bounded nonblocking event bridge must
be designed and reviewed. Its decision must identify the single non-render
coefficient writer; serialize host, UI and state-restore producers; define
event capacity, coalescing and overflow behavior; bound work on every render
callback; and define timestamp handling, publication latency and determinism.
An asynchronous worker alone does not establish deterministic application:
both online and offline rendering need an explicit timing policy and testable
ordering. This ADR does not choose a queue algorithm or claim that this gap is
already solved.

[ADR-004 sections (c) and (d)](ADR-004-parameters.md) remain binding:
targets are consumed at block boundaries and drive a fixed **20 ms internal
coefficient ramp**. Host-supplied ramp durations are flattened to target
changes under that contract. There is no claim of sample-accurate host
automation or host-ramp fidelity. A requirement for either needs an explicit
ADR-004 revision and a new bounded-work argument, not an incidental wrapper
implementation. Fixed-partition core determinism is not proof of deterministic
host-event scheduling.

## Decision and tradeoffs

Native Apple AUv3 APIs are the accepted direction for this scope, keeping all platform glue
outside the portable core. A single Apple format makes direct lifecycle and
event ownership a reasonable starting point. This deliberately accepts the
cost of writing and maintaining that glue. It is not evidence of lower CPU,
smaller binaries, faster delivery or better host reliability.

JUCE remains a credible alternative: it supports a separate C++ core and
CMake, supplies processor/format integration and offers a future UI path.
The current product scope does not yet justify adopting its framework and
licensing surface. Reconsider if approved platforms/formats expand or bounded
integration evidence shows its maintenance benefit outweighs those costs.

This acceptance selects only an architectural direction. It does not select
a UI toolkit, approve a production DSP configuration, resolve the event bridge,
choose buses/presets, or authorize code. The
[decision-only evaluation plan](../superpowers/plans/2026-09-18-auv3-integration-evaluation.md)
records the outstanding gates.

## Remaining decisions and later evidence

Before a separately authorized wrapper plan: resolve the parameter bridge;
production input/output layouts and dry/bypass behavior; state schema,
version/migration and restore policy; supported device/OS/rate/block-size
matrix; resource and failure behavior; Apple build/source ownership; and the
owner's signing/identifier/licence decisions as needed by that milestone.
Do not infer stereo-input support from the current evaluation route or
product preset compatibility from normalized values alone.

An eventual verification plan must name devices/hosts and record instantiation,
reconfiguration, zero/variable blocks, input-pull failures, bypass/tails,
reset/recovery, state recall, dense/conflicting automation, multiple instances,
offline repeatability, allocation/locking and callback-time evidence. None of
those checks has been performed by this documentation milestone.

## Revisit when

The owner changes format/platform scope; the event bridge cannot meet the
accepted timing/safety contract; real host/device evidence contradicts the
ownership assumptions; Apple or JUCE build/licensing requirements change; or
measured maintenance needs justify a different integration route.
