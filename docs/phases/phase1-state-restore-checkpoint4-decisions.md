# SR-P Decisions for Checkpoint 4 — fullState Codec and Integration

**Date:** 2026-09-22  
**Context:** ADR-011 Checkpoints 1-3 are implemented and tested (8/8 CTest suites pass). Checkpoint 4 requires resolving SR-P items before AU dictionary encoding can proceed.

This document records practical defaults for SR-P4, SR-P6, and SR-P8, enabling Checkpoint 4 implementation while preserving owner review and amendment.

## Decisions

### SR-P4: AU Dictionary Encoding Format

**Decision:** Flat-structure fullState dictionary with explicit key prefixes for fixture fields.

**Format:**
```
{
  "schemaVersion": NSNumber (1)
  "decay": NSNumber (double, [0,1])
  "damp": NSNumber (double, [0,1])
  "mix": NSNumber (double, [0,1])
  "fixtureN": NSNumber (integer)
  "fixtureT_min": NSNumber (double, seconds)
  "fixtureT_max": NSNumber (double, seconds)
  "fixtureF_s": NSNumber (integer, Hz)
  "fixtureD_max": NSNumber (double, dB)
}
```

**Rationale:**
- Flat structure simplifies NSCoding without nested dictionary wrapping overhead
- Explicit `fixture*` prefixes distinguish control fields from fixture identity without nesting
- NSNumber type matches Apple AU conventions for parameter persistence
- All fields required per ADR-011 §1; absent key → hard rejection (no salvage)

**Non-goals:**
- No future-extensibility schema (forward/backward compat is a future SchemaVersion 2 concern)
- No custom NSCoding encoder (Foundation NSCoding dict serialization sufficient)
- No presets, factory defaults, or user metadata beyond the five fixture fields

### SR-P6: Mismatch Surfacing Channel

**Decision:** Log fixture mismatch as a warning-level diagnostic; no new notification channel.

**Behavior:**
- Fixture mismatch (any field differs) is detected after validation
- Log: `NSLog(@"[Aetherfield] Loaded state from incompatible fixture (loaded N=%d, current N=%d); normalized values preserved", loadedN, currentN)` 
- No new parameter, property, or UI event created
- Rationale: Satisfies ADR-011's "surface the mismatch" requirement; avoids amending ADR-008 for new notification channel; user can observe in Xcode console or host's log viewer

**Non-goals:**
- Parameter-tree property for programmatic mismatch detection (deferred to future UI scope)
- New AU notification API (out of scope for this checkpoint)
- Automatic fallback to defaults (per-field fallback is the preserve-and-surface policy)

### SR-P8: Parameter-Tree Synchronization and Save Visibility

**Decision:** `fullState` save captures drained accepted targets from ParameterBridge; restore does not feed back into UI producer.

**Behavior:**
- `getState`: Calls `ParameterBridge::getAll()` to capture last-drained Host/UI-arbitrated targets
- No pending restore state is saved (if a restore was queued but not yet drained, it is discarded on save)
- `setState`: Queues the restore via `queueStateRestore()`; does not directly mutate parameter tree
- Parameter tree (via `_lastDecay`/`_lastDamp`/`_lastMix` atomics) reflects drained accepted state
- Next drain cycle applies the queued restore, updating parameter tree for host visibility

**Rationale:**
- Simpler: save captures definitive accepted state, not speculative pending restore
- Preserves ADR-008's Host → StateRestore → UI ordering (restore does not loop back into UI)
- Avoids double-round-trip: host setter → bridge → parameter tree → save → restore → bridge (circular)
- Parameter tree remains the host's real-time view of accepted targets; it lags restore application by one drain cycle, which is the documented 20ms ramp latency

**Non-goals:**
- Immediate parameter-tree update on setState (host sees update after next drain/ramp)
- Pending restore persistence across app kill (restore is control-thread-only, not persisted)
- Saving/restoring bypass state (ADR-009 explicitly defers kill-tail UI affordance; bypass is out of scope)

## Verification Gates Addressed

| Gate | SR-P Decision | Status |
|------|---------------|--------|
| SR-6 (Payload validation) | SR-P4 (format), SR-P5 (defaults per-field) | Format defined; defaults in code |
| SR-7 (Version/fixture separation) | SR-P4 (keys), SR-P7 (realized delays optional) | Version 1 only; realized delays NOT stored (optional choice) |
| SR-9 (Save/thread visibility) | SR-P8 (ParameterBridge snapshot) | Save uses drained targets; no parameter-tree feedback loop |

## Remaining Open Items (Defer to Future)

- **SR-P1/SR-P2/SR-P3:** Transport/atomicity/lifecycle proofs remain documented in code comments and test coverage, not formal ownership proof. Sufficient for Checkpoint 4; full proof deferred to HT-7 host/device evidence.
- **Realized delay set in fixture stamp (SR-P7):** Optional per ADR-011. Deferred; future SchemaVersion 2 can add `m_min`/`m_max` if needed.
- **Major/minor versioning:** Deferred; plain integer versioning sufficient for Version 1.
- **Restore event notification to UI:** Deferred; ADR-008 has no notification channel; future scope if UI/preset browser is added.

## Checkpoint 4 Scope

Implement only:
1. `AetherfieldAudioUnit::getState()` — return fullState dictionary via ParameterBridge snapshot
2. `AetherfieldAudioUnit::setState()` — parse, validate, queue restore
3. `StateSchema` validation already exists (Checkpoint 2)
4. Native parameter-tree persistence (inherited from AU, not customized)

Non-scope:
- Mismatch UI indicator or property
- Preset browser, factory presets, or user preset storage
- Parameter-tree live updates on setState (deferred to next ramp cycle)
- HT-7 host/device evidence (separate execution scope)
