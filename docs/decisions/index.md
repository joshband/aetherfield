# Architecture decision records

The canonical decisions are separate records. The legacy [decisions page](../decisions.md) retains the former headings for existing links.

| Record | Status | Implementation | Dependencies |
|---|---|---|---|
| [ADR-001 · Portable core](ADR-001-portable-core.md) | Accepted | Bootstrap implemented; AUv3 integration deferred | — |
| [ADR-002 · Late network](ADR-002-late-network.md) | Accepted | Fixed late network implemented as S2; product signal path deferred | ADR-001 |
| [ADR-003 · Numerical safety](ADR-003-numerical-safety.md) | Accepted | Fixed-network safety contract implemented as S2 | ADR-002 |
| [ADR-004 · Parameters](ADR-004-parameters.md) | Accepted | Parameter transitions implemented as PT; modulation deferred | ADR-002, ADR-003 |
| [ADR-005 · Evaluation fixture](ADR-005-evaluation-fixture.md) | Accepted | S2 fixture implemented; final product N deferred | ADR-002, ADR-003 |
| [ADR-006 · Diffusion and stereo](ADR-006-diffusion-stereo.md) | Accepted | Evaluation baseline implemented and measured: DS-B Tasks 1–4, DS-1…DS-13, control forwarding, and offline render tools; DS-B Sonic acceptance closed. Product/host work remains unauthorized. | ADR-002, ADR-003, ADR-004, ADR-005 |
| [ADR-007 · AUv3 integration comparison](ADR-007-auv3-integration-comparison.md) | Accepted (2026-09-18) | Native Apple APIs are the framework direction; no wrapper, UI or dependency implementation authorized | ADR-001, ADR-004, ADR-006 |
| [ADR-008 · Parameter-event bridge](ADR-008-parameter-event-bridge.md) | Accepted (2026-09-18) | Design only; no bridge/wrapper/UI/dependency implementation authorized | ADR-001, ADR-004, ADR-007 |
| [ADR-009 · Production bus policy](ADR-009-production-bus-policy.md) | Accepted (2026-09-18) | Architectural decision only; bus layout decided (stereo-in/stereo-out, sum-to-mono); no wrapper/UI/dependency implementation authorized | ADR-001, ADR-003, ADR-004, ADR-006, ADR-007 |
| [ADR-010 · Device/lifecycle matrix](ADR-010-device-lifecycle-matrix.md) | Proposed | Architectural decision only; no wrapper/UI/dependency implementation authorized | ADR-001, ADR-003, ADR-005, ADR-007 |

Every canonical record begins with YAML metadata. `status` distinguishes proposed from accepted architectural decisions. `implementation` records only the work actually completed for that record. `review` points to the current review state, and `depends_on` lists decisions whose contracts it relies on. An accepted decision does not authorize implementation.
