# Tasks 2–5 Host/Device Acceptance — Next Session Plan

**Previous Session:** 2026-09-22, Task 1 completed (HT-1/HT-3 passed on iPhone 16 Pro Max)

**Status:** Deferred due to iOS/AUM host integration friction. This document plans the next attempt with a clearer approach.

---

## Why Tasks 2–5 Were Deferred

Session 2026-09-22 attempted real-time audio testing via AUM (iOS) after Task 1's automated harness tests passed. Issues encountered:

| Issue | Root Cause | Impact |
|---|---|---|
| AU provisioning/discovery in AUM | iOS plugin cache, entitlements, or sandbox constraints | AU shows red X; UI unavailable |
| Audio routing failures | Sample rate negotiation blocked by host OS | Cannot change rates for HT-2 testing |
| No parameter UI access | AU partially instantiates but controls not exposed | Cannot set Decay/Damp/Mix for deterministic testing |

**Lesson:** Real-time host testing on iOS requires deeper integration work (provisioning profiles, entitlements, plugin cache management) that is orthogonal to DSP/AU correctness.

---

## Recommended Path: macOS AU + Reaper Headless

**Rationale:**
- Tasks 2–5 focus on real-time host integration, parameter automation, and performance measurement
- macOS AU avoids iOS plugin discovery friction
- Reaper supports full scriptability (MIDI, parameter events, offline export)
- Headless automation eliminates manual UI interaction

**Scope:**
- Build standalone macOS AU (not iOS extension)
- Create Reaper test scripts for each HT category
- Automate audio signal generation, routing, and analysis
- Measure performance via Instruments (HT-11/HT-12)

---

## Prerequisites for Next Session

Before starting Tasks 2–5:

- [ ] **Reaper installed** (confirmed present at `/Applications/REAPER.app`)
- [ ] **macOS AU build working** (separate target, Release configuration)
- [ ] **Test signal generation** (sine, impulse, white noise generators)
- [ ] **Reference audio analysis** (peak detection, FFT, silence detection)
- [ ] **Instruments profiling** (Xcode toolchain, symbols available)

---

## Task 2: Lifecycle, Buffers & Recovery (HT-2, HT-4, HT-6)

### HT-2: Rate Negotiation

**Purpose:** Verify AU rejects unsupported sample rates and preserves state across rate changes.

**Procedure (automated via Reaper + script):**

1. **Load project** in Reaper at 44.1 kHz with Aetherfield AU
2. **Set controls:** Decay=0.5, Damp=0.5, Mix=0.5
3. **Render 10 sec** → capture output as reference_44100.wav
4. **Change project rate** to 48.0 kHz (in-place, no AU reload)
5. **Render 10 sec** → capture output as resampled_48000.wav
6. **Compare:** Verify output continues smoothly (no clicks), tail behavior consistent
7. **Try 96 kHz:** Expect AU to reject or disconnect; verify state preserved

**Gate:** Smooth continuation across supported rates; rejection without fallback for unsupported rates.

---

### HT-4: Input Error & Missing Input

**Purpose:** Verify AU handles pull errors and missing input gracefully (no crash, no invalid output).

**Procedure (with test harness):**

1. **Initialize AU** with valid stereo input
2. **Render 10 sec** with normal input → capture output
3. **Inject pull error:** Return error code from input callback
4. **Continue render** for 10 sec (AU should gracefully degrade or output silence/tail)
5. **Resume valid input:** Verify seamless reconnection, no pops/clicks
6. **Verify:** No crash, no non-finite samples (NaN/Inf), no blocking on missing input

**Gate:** Graceful error handling, output continuity without crashes.

**Note:** True pull-error injection requires test harness; Reaper's native error simulation may be limited. Consider hybrid: harness + analysis.

---

### HT-6: Host Reset & Self-Recovery

**Purpose:** Verify AU consumes explicit host reset requests and recovers from internal faults.

**Procedure:**

1. **Render baseline** 10 sec at settled controls
2. **Trigger host reset** (Reaper MIDI panic or AU reset callback via script)
3. **Continue rendering** without stopping engine
4. **Observe:** Output mutes to silence, then resumes from clean silence (no pops)
5. **Verify:** Reset flag is consumed (no stale reset state on next render)
6. **Self-recovery (optional):** Inject NaN/Inf via input, drop to silence, verify recovery

**Gate:** Reset mutes output exactly, resumes cleanly, no stale state.

---

## Task 3: Hybrid Bypass & Automation (HT-5, HT-8)

### HT-5: Bypass Transitions

**Purpose:** Verify dry/wet switching is click-free and tail is properly managed during bypass.

**Procedure:**

1. **Precondition:** 10 sec of input at Decay=1, Mix=0.5 (wet-only tail present)
2. **Render normal** 5 sec → baseline output
3. **Activate bypass** → AU should stop processing, output dry signal only
4. **Listen for clicks:** No pops at bypass toggle
5. **Drain time:** Measure time to exact silence after bypass (verify ADR-009 `T_silence` bound)
6. **Un-bypass:** Resume processing from known silence, no glitches
7. **Live tail measurement:** Compare drained tail against identically advanced zero-input reference

**Gate:** Zero clicks on bypass toggle, exact silence within `T_silence` bound, clean reactivation.

---

### HT-8: Parameter Automation

**Purpose:** Verify AU handles dense parameter events and host automation correctly.

**Procedure:**

1. **Setup:** Create Reaper envelope automation for Decay and Mix
2. **Render with dense events:** 100 parameter updates per render block for 10 sec
3. **Capture output:** Reference render with automation
4. **Measure latency:** Time from event receipt to first affected sample
5. **Verify delivery:** Last-write-wins semantics (repeated parameter writes collapse correctly)
6. **Measure mailbox updates:** Verify at most 3 Host-side mailbox writes per block (ADR-008 contract)

**Gate:** Correct event ordering, last-write-wins semantics, latency < 1 block, mailbox efficiency.

---

## Task 4: Instances & Offline (HT-9, HT-10)

### HT-9: Multi-Instance Isolation

**Purpose:** Verify multiple AU instances don't interfere with each other.

**Procedure:**

1. **Create two parallel chains** in Reaper, each with Aetherfield AU
2. **Set different controls:** Chain A (Decay=0.25, Mix=1), Chain B (Decay=1, Mix=0.5)
3. **Render 30 sec** with independent stereo inputs
4. **Compare to single-instance runs:** Each chain's output must match its solo render bit-exactly
5. **Measure capacity:** 1, 2, 4, 8 instances, measure CPU/memory per count on macOS

**Gate:** Bit-exact isolation, linear CPU scaling, no crosstalk.

---

### HT-10: Offline Determinism

**Purpose:** Verify offline export renders identically across multiple runs (no async state).

**Procedure:**

1. **Setup:** Reaper offline render mode, 48 kHz, fixed controls (Decay=0.5, Damp=0.5, Mix=0.5)
2. **Export 1:** Render full 60 sec project → export_run1.wav
3. **Export 2:** Same settings, same project → export_run2.wav
4. **Export 3:** Same settings, same project → export_run3.wav
5. **Compare:** All three byte-identical (or float bit-identical if lossless format)
6. **Measure metadata:** File timestamps, sample counts, hashes

**Gate:** Byte-exact repeatability across exports (no async publication, no state leakage).

---

## Task 5: Callback Audit & Timing (HT-11, HT-12)

### HT-11: Callback Allocation & Lock Audit

**Purpose:** Verify render callback is allocation-free and lock-free (no blocking operations).

**Procedure (Instruments):**

1. **Build for macOS** with Debug symbols, release optimizations
2. **Open Instruments** on macOS
3. **Record session:** Run AU in Reaper with Allocations + Thread State instruments
4. **Render 60 sec** with varied block sizes `{32, 64, 128, 256, 512}`
5. **Analyze output:**
   - Zero allocations during callback (allowed: pre-allocation only)
   - Zero lock acquisitions (atomic ops only)
   - No blocking I/O or system calls
6. **Audit call graph:** Manual review of every function called from callback

**Gate:** Zero allocations, zero locks, zero blocking operations on audio thread.

---

### HT-12: Performance & Timing Distribution

**Purpose:** Measure callback execution time, CPU headroom, and thermal behavior.

**Procedure:**

1. **Setup Reaper** at various block sizes and sample rates
2. **Configure Instruments:** Timeline, CPU, Thermal, Power instruments
3. **Render representative load:**
   - 10 min each @ {32, 64, 128, 256, 512} frames, both 44.1 kHz and 48 kHz
   - Dense automation (parameter updates every callback)
   - High-energy input (white noise at 0.8 amplitude)
4. **Capture metrics per block size:**
   - Min/median/p95/p99/max callback duration (ms)
   - CPU % per instance
   - Thermal state, power draw (if available)
   - Zero underruns/buffer overflows
5. **Deadline calculation:** frame_count / sample_rate (e.g., 512 @ 48 kHz = 10.67 ms)
   - Verify max duration < 80% deadline (headroom)

**Gate:** No underruns, max duration < deadline × 0.8, acceptable CPU footprint.

---

## Reference Materials

- **ADR-008:** Parameter bridge mailbox contract (3 writes/block limit)
- **ADR-009:** Hybrid bypass, `T_silence(decay)` bound, diffusion cascade drain
- **ADR-011:** State restore, atomic 3-parameter publish (unrelated to Tasks 2–5 but reference for future HT-7)
- **Reaper JSFX/EEL:** Script language for parameter automation and testing
- **Instruments:** Allocations, Thread State, Timeline, CPU, Thermal, Power instruments

---

## Execution Checklist (Next Session)

- [ ] **Environment setup**
  - [ ] macOS AU target created/builds successfully
  - [ ] AU loads in Reaper without errors
  - [ ] Test signal generation working (sine, impulse, noise)
  - [ ] Audio analysis framework ready (peak, FFT, silence detection)
  - [ ] Instruments profiling symbols available

- [ ] **Task 2 (HT-2/4/6)**
  - [ ] HT-2: Rate negotiation script created, tests run, results documented
  - [ ] HT-4: Input error harness integrated, tests run, results documented
  - [ ] HT-6: Reset injection method confirmed, tests run, results documented

- [ ] **Task 3 (HT-5/8)**
  - [ ] HT-5: Bypass toggle script, tail measurement, tests run, results documented
  - [ ] HT-8: Automation script, event density test, latency measured

- [ ] **Task 4 (HT-9/10)**
  - [ ] HT-9: Multi-instance script, capacity test, results documented
  - [ ] HT-10: Offline export repeatability test, hashes compared

- [ ] **Task 5 (HT-11/12)**
  - [ ] HT-11: Instruments session recorded, callback audit complete
  - [ ] HT-12: Performance timing distribution measured, headroom verified

- [ ] **Documentation**
  - [ ] Results published in testing.md with exact commands, metrics, hashes
  - [ ] Any blockers or unexpected findings flagged for owner review
  - [ ] Artifacts (traces, audio files, Reaper projects) archived per evidence contract

---

## Known Unknowns (To Resolve Next Session)

1. **macOS AU build complexity** — How much refactoring needed? (1 hour? 1 day?)
2. **Reaper script capabilities** — Can JSFX/EEL reach all required test scenarios?
3. **Instruments profiling on macOS** — Symbol resolution, callback attribution accuracy?
4. **Performance baselines** — What CPU % is "acceptable" for a reverb AU? (owner/product decision)

---

## Success Criteria

✅ **Task 2–5 complete if:**
- All required HT subcategories (HT-2, 4, 6, 5, 8, 9, 10, 11, 12) have pass/fail/unmeasured results
- Results are reproducible (commands documented, audio hashes recorded)
- No unexpected crashes or data corruption
- Any failures are documented with root cause analysis
- Owner reviews findings and approves conclusions

---

## Fallback Options (If macOS Path Blocked)

1. **Stay on iOS but use XCTest harness only** — Accept that real-time host testing is out of scope; rely on simulator/harness testing for Tasks 2–5
2. **Use JUCE wrapper instead** — Rebuild AU with JUCE framework (cross-platform), test on macOS + Windows (but violates ADR-007)
3. **Portable C++ test harness** — Create non-host-based testing framework for Tasks 2–5 (less realistic, more control)

---

## Next Session Start

```
Read:
1. This plan (TASKS_2-5_NEXT_SESSION_PLAN.md)
2. ADR-008, ADR-009, ADR-011
3. Task 1 results in testing.md

Execute:
1. Build macOS AU
2. Verify in Reaper
3. Run Task 2 (HT-2/4/6)
4. Continue Task 3–5 as time allows
5. Document findings in testing.md
```
