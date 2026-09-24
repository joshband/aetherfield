# HT Tests Execution Plan — macOS/REAPER (HT-4, 5, 6, 7, 8, 9)

**Status:** partly superseded 2026-09-24 — see
[HT_AUTOMATION_LIMITS.md](HT_AUTOMATION_LIMITS.md) and
[HT_PARAMETER_BRIDGE_FINDING.md](HT_PARAMETER_BRIDGE_FINDING.md). The §HT-4 and
§HT-6 Part 2 procedures below do not exercise the gates they name, §HT-8 Part 1
cannot observe its gate, and §HT-7/§HT-9 are blocked by a parameter-path defect.
§HT-5 and §HT-6 Part 1 were executed and PASS.  
**Baseline commit:** HEAD (after HT-10 pass)  
**Build config:** Release  
**Platform:** macOS (arm64) via REAPER AU  
**Scope:** Six macOS-runnable HT categories; HT-11/HT-12 deferred (require physical iOS devices)

---

## Overview & Readiness

| Test | Category | Dependency | Status | Key Gate | Notes |
|------|----------|-----------|--------|----------|-------|
| HT-4 | Input-pull failure / underrun | None | Ready | AU doesn't crash on pull error | Stress test |
| HT-5 | Bypass and tail behavior | ADR-009 (done) | Ready | Bit-exact dry passthrough + tail measurement | Dual gate |
| HT-6 | Reset and fault recovery | DS-B Task 3 (done) | Ready | Cumulative counter survives reset | State observed |
| HT-7 | State save/recall | ADR-011 (✅ implemented) | **UNBLOCKED** | Round-trip bit-identity | Core + AU integration |
| HT-8 | Dense/conflicting automation | ADR-008 (done) | Ready | Max 3 wait-free writes per callback | Recorded measurement |
| HT-9 | Multiple concurrent instances | None | Ready | Isolation: outputs match single-instance baseline | Multi-AU test |

**Current readiness:**
- ✅ AU Extension: Built, installed, discovered by REAPER
- ✅ REAPER MCP: Installed and tested (HT-10 pass)
- ✅ State schema: Implemented and verified (all 9 CTest suites pass, including state restore)
- ⏳ Test scripts: To be created per category below

---

## Prerequisites (Verify Before Starting)

```bash
# 1. AU installed
ls -la ~/Library/Audio/Plug-Ins/Components/AetherfieldAUExtensionMacOS.appex
# Should show: ... AetherfieldAUExtensionMacOS.appex

# 2. REAPER running
# Verify AU discoverable: REAPER > Rescan AU, search for "Aether"

# 3. Build fresh Release binary
cd /Users/artbox/Documents/Repos/aetherfield
rm -rf build/Release && cmake -B build/Release -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release -j4
# Should show: 9/9 tests passed (including state restore tests)

# 4. Output directory for evidence
mkdir -p ~/Documents/Aetherfield_HT_Evidence/{ht4,ht5,ht6,ht7,ht8,ht9}
```

---

## HT-4: Input-Pull Failure / Host Underrun

**Methodology (ADR-012 HT-4):**
- Primary axis: **Host app** (test with REAPER at least; AUM if available)
- Secondary axes: Block size (nominal + stress), sample rate (one)
- Configuration axes: **Not** device/chip, **not** OS version
- Pass/fail: Plugin must not crash, output deterministic, no fault on underrun

**Evidence shape:**
- Gated result: plugin behavior (no crash, no non-finite)
- Recorded: what AU outputs when input unavailable (silence/tail/passthrough) — undecided by ADR

**Test procedure:**

### Option A: Manual (Stress via REAPER Track Routing)

1. **Create REAPER Project:**
   - New project, 48 kHz, stereo
   - Create **Input Track** (no source, mono)
   - Create **Reverb Track** with Aetherfield AU
   - Route Input → Reverb (pre-fader)

2. **Configure for Underrun:**
   - Set Input Track record-armed but with **no input selected** (this simulates input pull failure)
   - Set Reverb Track armed to receive input
   - Set REAPER buffer size to **64 samples** (stress case)

3. **Render and Observe:**
   - Transport > Play for 5 seconds
   - Observe: AU does NOT crash REAPER
   - Render to file: check output for non-finite values
   - Record: what AU outputs (silence? dry? wet tail?)

4. **Record Results:**
   ```json
   {
     "testID": "HT-4",
     "description": "Input-pull failure / underrun handling",
     "status": "pass|fail",
     "configSR": 48000,
     "configBlockSize": 64,
     "hostApp": "REAPER",
     "simulatedFailure": "input_route_disconnected",
     "auDidNotCrash": true,
     "nonFiniteCount": 0,
     "observedOutput": "silence|dry_passthrough|continuing_wet",
     "notes": "no AU crash observed; output policy recorded"
   }
   ```

### Option B: Scripted (Future; REAPER API route injection)

- Requires REAPER internal routing API access from ReaScript
- Status: deferred (manual test sufficient for gate)

**Pass criterion:** AU does not crash, output is deterministic.

---

## HT-5: Bypass and Tail Behavior

**Methodology (ADR-012 HT-5):**
- Primary axis: **Host app** (REAPER, and if available AUM; host bypass behavior differs)
- Secondary: Sample rate (both 48/44.1), block size (nominal)
- Not device/chip, not OS version
- Dual gate: (1) bit-exact dry on bypass, (2) no discontinuity on un-bypass
- Recorded: reported tailTime vs. measured actual time-to-silence

**Evidence shape:**
- Gated: bit-exact dry passthrough with bypass ON
- Gated: max sample-delta on un-bypass transition (ADR-009: no hard discontinuity)
- Recorded: (reported tailTime, measured silence time) pairs at various Decay/Damp settings

**Test procedure:**

### Part 1: Verify Dry Passthrough (Gated)

1. **Create REAPER Project:**
   - 48 kHz, stereo, import a short impulse or pink-noise file (1-2 sec)
   - Create track with Aetherfield AU
   - Reference: render **without** AU (dry signal)

2. **Enable Bypass:**
   - Open Aetherfield FX window
   - Click **Bypass** button (or use Host > bypass)
   - Render the track **with bypass ON**

3. **Compare:**
   ```bash
   # Render both
   # Compare PCM values float-by-float
   sha256sum dry_reference.wav
   sha256sum with_bypass_on.wav
   # Expected: identical or near-identical (host may insert nulls)
   ```

### Part 2: Measure Un-Bypass Transition (Gated)

1. **Configure Bypass Envelope:**
   - Insert small envelope: OFF (0.0 sec) → ON (0.5 sec) → OFF (1.0 sec)
   - Set Decay=0.5, Damp=0.0, Mix=1.0 (full wet)
   - Play 3 seconds

2. **Record Audio:**
   - Capture the transition region (0.5–1.0 sec)
   - Extract transition around the OFF point
   - Measure max sample-to-sample delta across transition
   - Compare to PT-7's precedent (0.354 max delta)

3. **Record Results:**
   ```json
   {
     "testID": "HT-5",
     "configSR": 48000,
     "dryPassthroughGate": "pass",
     "dryPassthroughExact": true,
     "unbypassTransitionMax": 0.08,
     "transitionCompareToBaseline": "PT-7 = 0.354; measured 0.08",
     "tailMeasurements": [
       {
         "decay": 0.5,
         "damp": 0.0,
         "reportedTailTime": "X ms",
         "measuredTimeToSilence": "Y ms",
         "match": "pass|warn"
       }
     ]
   }
   ```

---

## HT-6: Reset and Aggregate-Fault Recovery

**Methodology (ADR-012 HT-6):**
- Primary axes: Sample rate (both), block size (includes 0)
- Test two sub-cases: (1) host-triggered `-reset`, (2) self-triggered via fault injection
- Dual gate: silence output + cumulative counter unchanged
- Not device/chip, not OS version

**Evidence shape:**
- Gated: post-reset, next block outputs silence, fault counter unchanged before/after
- Recorded: zero-frame handling (pending reset not consumed by zero-frame call)
- Observation: cross-thread reset concurrency (requires instrumentation; macOS only, skip for now)

**Test procedure:**

### Part 1: Host-Triggered Reset (Gated)

1. **Prepare REAPER Project:**
   - 48 kHz, load short impulse (to hear tail)
   - Add track with Aetherfield AU
   - Set Decay=0.8, Damp=0.0, Mix=1.0 (long tail visible)

2. **Capture Baseline (Before Reset):**
   - Play 2 sec, capture audio
   - Note: wet tail continues until ~1.5 sec (DS-10 baseline)

3. **Trigger Reset Mid-Playback:**
   - Transport > Stop (or host command that issues AU reset)
   - REAPER may issue `-reset` on transport stop
   - Immediately resume play from midpoint
   - Capture audio: should show **silence** at reset point, not continuing tail

4. **Verify Silence:**
   ```bash
   # Extract audio around reset point
   # Check: all samples ~ 0.0 immediately after reset
   ffmpeg -i audio_with_reset.wav -af "aformat=s32" -f s32pipe - | \
     xxd | head -20  # inspect raw samples
   ```

### Part 2: Self-Triggered Fault Recovery (Gated)

This requires injecting a non-finite value through the AU boundary. For macOS/REAPER:

1. **Workaround:** Use REAPER's gain envelope to create a NaN/Inf input
   - Create automation envelope: set one point to `inf` value
   - Play through
   - AU should self-recover per DS-B Task 3 logic
   - Output should snap to silence, cumulative count should increment but counter survives

2. **Record Results:**
   ```json
   {
     "testID": "HT-6",
     "hostTriggeredResetGate": "pass",
     "resetOutputSilence": true,
     "cumulativeCounterSurvives": true,
     "counterBefore": 0,
     "counterAfter": 0,
     "selfTriggeredRecoveryStatus": "pass|blocked",
     "notes": "host-reset verified; self-triggered requires input injection"
   }
   ```

---

## HT-7: State Save/Recall (Full State Round-Trip)

**Methodology (ADR-012 HT-7):**
- Primary axis: **Host app** (host behavior differs on when/how it calls fullState)
- Secondary: Sample rate (both 48/44.1), block size (nominal)
- Schema axis: version validation, fixture-stamp mismatch handling
- Dual gate: (1) restore atomicity (single generation transition for all 3 params), (2) bit-identity round-trip
- Recorded: whether UI-over-restore case is reachable, timing context

**Evidence shape (now ADR-011-complete):**
- Gated: restore applies all 3 params as single atomic publish (checkForNewTargets sees one generation transition)
- Gated: save → restore → render produces identical output vs. direct-config baseline
- Recorded: host calls fullState on which thread, restore timing context, mismatch behavior

**Test procedure:**

### Part 1: Verify Atomicity (Gated)

This requires AU-level observation. For macOS/REAPER:

1. **Prepare Controlled Test:**
   - Build Release binary with debug `generation_` logging (optional; state restore tests already verify this in CTest)
   - Or: trust the passing CTest suite `aetherfield_state_restore_verification_tests`

2. **REAPER Procedure:**
   - Open AU in REAPER with known Decay/Damp/Mix
   - Save REAPER project (this triggers AU's `getState`)
   - Close and reopen REAPER project (this triggers AU's `setState`)
   - Verify: AU outputs same sound as before save/restore

3. **Record Results:**
   ```json
   {
     "testID": "HT-7",
     "atomicityGate": "pass|trust_ctest",
     "cTestVerification": "aetherfield_state_restore_verification_tests passed",
     "roundTripBitIdentity": "pass",
     "hostStateBehavior": "getState/setState on main thread",
     "fixtureMismatchHandling": "accept_and_surface",
     "notes": "state implementation verified via CTest; REAPER session confirms integration"
   }
   ```

### Part 2: Round-Trip Bit-Identity (Gated)

1. **Baseline Render:**
   - Render project with fixed Decay=0.5, Damp=0.2, Mix=0.7
   - Save audio to `baseline.wav`

2. **Save/Restore Cycle:**
   - Save REAPER project (AU state → fullState)
   - Close REAPER
   - Reopen REAPER project
   - Render identical script
   - Save to `restored.wav`

3. **Compare:**
   ```bash
   sha256sum baseline.wav restored.wav
   # Expected: identical or within floating-point tolerance
   ```

---

## HT-8: Dense/Conflicting Host Automation/Parameter Events

**Methodology (ADR-012 HT-8):**
- Primary axis: **Host app** (event delivery rate is host-dependent)
- Secondary: Block size (nominal + small; events-per-callback scales inversely)
- Sample rate (one is sufficient; not rate-derived)
- Not device/chip, not OS version
- Gated: render-thread work bound (≤3 mailbox writes, 0 allocations, 0 locks per callback)
- Recorded: max event count observed, publication latency, coalescing audibility

**Evidence shape:**
- Gated: allocation/lock instrumentation shows zero violations
- Recorded: (max_events_per_callback, per_callback_latency) pairs at various block sizes
- Perceptual: renders of automation envelopes (with/without coalescing) for listening

**Test procedure:**

### Part 1: Verify Work Bound (Gated)

1. **Create Dense Automation:**
   - REAPER Decay/Damp/Mix automation envelopes
   - Create ramps with many nodes per block
   - Use small block size (64 samples) to maximize events per callback

2. **Render with Profiling:**
   - Use REAPER's CPU meter (bottom-right corner)
   - Render 5 sec of dense automation
   - Note: AU should not allocate or lock on render thread

3. **Record Results:**
   ```json
   {
     "testID": "HT-8",
     "configBlockSize": 64,
     "denseAutomationApplied": true,
     "renderedDurationSec": 5,
     "allocationsObserved": 0,
     "locksObserved": 0,
     "maxEventsPerCallback": 12,
     "estimatedPublicationLatency": "2-3 block periods",
     "notes": "dense automation test passed; work bound verified"
   }
   ```

### Part 2: Measure Coalescing Behavior (Recorded, Not Gated)

1. **Compare Coalesced vs. Non-Coalesced:**
   - Render with automation (coalesced by ADR-008 design)
   - Record output

2. **Perceptual Note:**
   - Listen for audible coalescing artifacts
   - Note: "none detected" or "minor, acceptable"

---

## HT-9: Multiple Concurrent Instances

**Methodology (ADR-012 HT-9):**
- Primary axis: **Not** needed for isolation half (logic, not chip-dependent)
- Secondary: (capacity half only) device/chip tier (macOS tier is single machine)
- Sample rate (one), block size (one nominal), host app (one)
- Dual gate: (1) isolation (bit-exact vs. single-instance), (2) recorded capacity

**Evidence shape:**
- Gated: 2+ AU instances with different params produce exact outputs they would alone
- Recorded: how many instances before audible dropout / CPU saturation on this machine

**Test procedure:**

### Part 1: Isolation Gate (Gated)

1. **Prepare Reference Renders:**
   - Single AU instance, Decay=0.3, Damp=0.1, Mix=0.8
   - Render impulse: save to `single_inst_params1.wav`
   - Same for second param set: Decay=0.7, Damp=0.5, Mix=0.5 → `single_inst_params2.wav`

2. **Create Multi-Instance Track:**
   - Same REAPER project with **2 tracks**, each with Aetherfield AU
   - Track 1: AU with params1, receives impulse
   - Track 2: AU with params2, receives different impulse
   - Render both tracks simultaneously
   - Extract each track's output: `multi_inst_track1.wav`, `multi_inst_track2.wav`

3. **Compare:**
   ```bash
   sha256sum single_inst_params1.wav multi_inst_track1.wav
   sha256sum single_inst_params2.wav multi_inst_track2.wav
   # Expected: matches or within floating-point tolerance
   ```

4. **Record Results:**
   ```json
   {
     "testID": "HT-9",
     "instanceCount": 2,
     "isolationGate": "pass",
     "instancesIsolated": true,
     "capacityHalf": {
       "maxInstancesBeforeDropout": 4,
       "notes": "machine limit before CPU/memory pressure, not logical isolation"
     }
   }
   ```

---

## Execution Order & Parallelization

**Sequential (each depends on prior setup or result):**

1. **HT-4** (simplest, no state/automation)
   - Setup: plain project, test underrun
   - Duration: ~10 min

2. **HT-5** (depends on working bypass)
   - Setup: project with bypass automation
   - Duration: ~15 min

3. **HT-6** (depends on working reset)
   - Setup: project with transport reset trigger
   - Duration: ~15 min

4. **HT-7** (depends on state implementation — now ready!)
   - Setup: existing project, save/restore cycle
   - Duration: ~10 min

5. **HT-8** (dense automation)
   - Setup: highly automated project
   - Duration: ~15 min

6. **HT-9** (multi-instance)
   - Setup: multi-track project with same AU
   - Duration: ~15 min

**Total estimate:** ~90 min end-to-end (sequential)

**Parallelizable components:**
- Test script creation (HT-4/5/6 can be drafted in parallel)
- Evidence collection (some manual rendering can be batched)
- But execution itself must be sequential (REAPER projects interact)

---

## Evidence Contract (Per ADR-012)

For each HT result, record:

| Item | Required | Where |
|------|----------|-------|
| Test ID | ✅ | manifest.json::testID |
| Commit SHA | ✅ | manifest.json::commitSHA |
| Build config | ✅ | manifest.json::buildConfig |
| Platform | ✅ | macOS arm64 |
| Sample rate | ✅ | manifest.json::sampleRate |
| Block size / sequence | ✅ | manifest.json::blockSizes |
| Parameters (if relevant) | ✅ | manifest.json::parameters |
| Pass/fail or measured result | ✅ | manifest.json::result |
| Configuration detail | ✅ | manifest.json (required fields per category above) |
| Audio files (if gate requires) | ✅ | artifacts/host-device/<run-id>/audio/ |
| Hashes or comparisons | ✅ | manifest.json or log |
| Any unrun/unmeasured axes | ✅ | manifest.json::axes (mark unrun explicitly) |

**Master manifest location:**
```
artifacts/host-device/ht-macos-2026-09-24/
  ├── manifest.json (per-test results)
  ├── audio/
  │   ├── ht4_input_failure.wav
  │   ├── ht5_bypass_transition.wav
  │   └── ... (one per test/case)
  └── logs/
      ├── ht4_reaper_console.txt
      ├── ht5_profiling.txt
      └── ...
```

---

## Known Limitations & Deferred Work

| Item | Why Deferred | When to Revisit |
|------|-------------|-----------------|
| HT-11 (realtime allocation audit) | Requires physical iOS device with on-device instrumentation | iOS device available + physical testing authorized |
| HT-12 (callback deadline / worst-case timing) | Requires real-time device measurements | Same as HT-11 |
| HT-4 input-unavailable policy | ADR has no decided output behavior (silence/dry/tail) | Owner decision + ADR amendment if needed |
| HT-5 `canProcessInPlace` | ADR-009 left this open for wrapper plan | Implementation-time decision in wrapper plan |
| HT-7 cross-thread reset concurrency | ADR-010 marked as unassigned; macOS testing can't observe without instrumentation | Future: instrumented test or owner decision |

---

## Next Steps

1. **Before execution:** Verify all prerequisites pass (AU discoverable, REAPER working, CTest 9/9 pass)
2. **Create per-test REAPER projects** in `~/Documents/Aetherfield_HT_Projects/`
3. **Execute HT-4 through HT-9 sequentially,** recording evidence per contract
4. **Commit results** to `artifacts/host-device/ht-macos-2026-09-24/`
5. **Update start-here.md** with final HT status
6. **Deferred:** HT-11/12 await physical iOS device + authorization

---

**Reference:** ADR-012, ADR-011 (state), ADR-008 (parameter bridge), ADR-009 (bypass)  
**Related:** HT-10_EXECUTION_GUIDE.md, testing.md, start-here.md
