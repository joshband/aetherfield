# Architectural decisions

This compatibility page preserves the former decision-record headings and links to the canonical ADR files. New references should use the canonical records and the [ADR index](decisions/index.md).

## ADR-001 — Portable C++ host loop; AUv3 framework decision deferred

Canonical record: [ADR-001](decisions/ADR-001-portable-core.md)

## ADR-002 — Late reverb network: FDN with an orthogonal feedback matrix and per-line damping

Canonical record: [ADR-002](decisions/ADR-002-late-network.md)

## ADR-003 — Damping filter, denormal mitigation, numerical bounds, and full-network lifecycle

Canonical record: [ADR-003](decisions/ADR-003-numerical-safety.md)

## ADR-004 — Initial parameter set: units, mappings, smoothing, transport, and the automation/modulation boundary

Canonical record: [ADR-004](decisions/ADR-004-parameters.md)

## ADR-005 — S2 evaluation fixture: FDN line count, the concrete delay-length set, and the fixture's sample rates

Canonical record: [ADR-005](decisions/ADR-005-evaluation-fixture.md)

## ADR-006 — Input diffusion, network injection, output tap design and stereo decorrelation

Canonical record: [ADR-006](decisions/ADR-006-diffusion-stereo.md)

## ADR-007 — Native Apple APIs versus JUCE for iOS/iPadOS AUv3

Canonical record: [ADR-007](decisions/ADR-007-auv3-integration-comparison.md).
Accepted (2026-09-18): native Apple APIs are the framework direction. No wrapper, UI or dependency implementation authorized.

## ADR-008 — Nonblocking parameter-event bridge for host/UI-to-render-thread control delivery

Canonical record: [ADR-008](decisions/ADR-008-parameter-event-bridge.md).
Accepted (2026-09-18). Design only; no bridge/wrapper/UI/dependency implementation authorized.

## ADR-009 — Production audio bus layout, dry/bypass, and buffer-aliasing policy

Canonical record: [ADR-009](decisions/ADR-009-production-bus-policy.md).
Accepted (2026-09-18): stereo-in/stereo-out bus with sum-to-mono reduction. No wrapper/UI/dependency implementation authorized.

## ADR-010 — Supported device/OS/rate/block-size matrix and AUv3 lifecycle/failure scope

Canonical record: [ADR-010](decisions/ADR-010-device-lifecycle-matrix.md).
Proposed. Architectural decision only; no wrapper/UI/dependency implementation authorized.
