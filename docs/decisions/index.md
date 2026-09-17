# Architecture decision records

The canonical decisions are separate records. The legacy [decisions page](../decisions.md) retains the former headings for existing links.

| Record | Status | Implementation | Dependencies |
|---|---|---|---|
| [ADR-001 · Portable core](ADR-001-portable-core.md) | Accepted | Bootstrap implemented; AUv3 integration deferred | — |
| [ADR-002 · Late network](ADR-002-late-network.md) | Accepted | Fixed late network implemented as S2; product signal path deferred | ADR-001 |
| [ADR-003 · Numerical safety](ADR-003-numerical-safety.md) | Accepted | Fixed-network safety contract implemented as S2 | ADR-002 |
| [ADR-004 · Parameters](ADR-004-parameters.md) | Accepted | Parameter transitions implemented as PT; modulation deferred | ADR-002, ADR-003 |
| [ADR-005 · Evaluation fixture](ADR-005-evaluation-fixture.md) | Accepted | S2 fixture implemented; final product N deferred | ADR-002, ADR-003 |
| [ADR-006 · Diffusion and stereo](ADR-006-diffusion-stereo.md) | Accepted | Evaluation baseline; DS-B Tasks 1, 2a, 2b, and 3 implemented; Task 4 measurements need separate authorization | ADR-002, ADR-003, ADR-004, ADR-005 |

Every canonical record begins with YAML metadata. `status` records the accepted architectural decision. `implementation` records only the work actually completed for that record. `review` points to the current review state, and `depends_on` lists decisions whose contracts it relies on. An accepted decision does not authorize implementation.
