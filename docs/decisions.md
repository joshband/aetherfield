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
Accepted (2026-09-18): stereo-in/stereo-out bus with sum-to-mono reduction. Also decided, 2026-09-19: sum-to-mono is an explicit interim step; bypass CPU-vs-tail-continuity is a hybrid approach (not yet designed); multi-channel/surround is not a target; `canProcessInPlace = true` is a recommendation pending implementation-time confirmation. Kill-tail-on-bypass remains explicitly deferred until UI is scoped. No wrapper/UI/dependency implementation authorized.

## ADR-010 — Supported device/OS/rate/block-size matrix and AUv3 lifecycle/failure scope

Canonical record: [ADR-010](decisions/ADR-010-device-lifecycle-matrix.md).
Accepted (2026-09-18): minimum deployment target is "current major version minus one" (iOS/iPadOS 26+, corrected 2026-09-19 from the 25+ originally recorded — see ADR-010's "Correction note"). No wrapper/UI/dependency implementation authorized.

## ADR-011 — Persisted state payload, schema versioning, migration and restore semantics

Canonical record: [ADR-011](decisions/ADR-011-state-schema.md).
Accepted (2026-09-18): payload shape (three normalized doubles, `schemaVersion`, a fixture stamp extended with `D_max`), per-field-fallback validation, an accept-and-surface fixture-mismatch policy, and atomic 3-parameter restore (reopening ADR-008's alternative (C) for the StateRestore role). Names two unimplemented `ParameterAutomation` obligations. No wrapper/UI/dependency implementation authorized.

## ADR-012 — Host/device acceptance-test catalog: methodology for HT-1…HT-12, device/OS scope policy, and tooling scope

Canonical record: [ADR-012](decisions/ADR-012-host-device-acceptance-catalog.md).
Accepted (2026-09-18): methodology for ADR-010's twelve named HT categories, plus device/OS scope policy (D) — corners, with the weakest tier added for the two timing categories. Device/chip-tier scope decided 2026-09-19: per-family corners (iPhone SE 2nd gen/A13 + newest iPhone; iPad 8th gen/A12 + newest iPad Pro), re-verify "newest available" at implementation-plan time. No test/host/device/CI implementation authorized.
