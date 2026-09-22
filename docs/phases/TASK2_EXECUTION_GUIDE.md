# Task 2 Execution Guide — HT-2, HT-4, HT-6

**Prerequisite:** AUM (Audio Mixer Ultimate) v5.0+ installed on iPhone 16 Pro Max

## Quick Setup Checklist

- [ ] Launch AUM on iPhone 16 Pro Max
- [ ] Verify Aetherfield AU is discoverable in AUM's plugin browser (Reverb/Josh)
- [ ] Create a new session/document in AUM with Aetherfield AU inserted
- [ ] Set input to microphone or test signal generator (if available)
- [ ] Set output to speaker or headphones
- [ ] Confirm audio routing: Input → Aetherfield AU → Output

## Task 2 Overview

| Category | Test | Duration | Setup |
|---|---|---|---|
| **HT-2** | Rate negotiation (44.1↔48kHz, reject invalid) | ~5 min | AUM rate selector + state comparison |
| **HT-4** | Input error injection and missing input | ~10 min | Disable input, pull error simulation |
| **HT-6** | Host reset request and self-recovery | ~10 min | Trigger reset via AUM controls |

## HT-2: Rate Changes & Rejection

**Purpose:** Verify AU preserves state during rate changes and correctly rejects unsupported rates.

**Procedure:**

1. **Establish baseline at 44.1 kHz**
   - In AUM: Project → Project Settings → Sample Rate → 44100 Hz
   - Insert Aetherfield AU
   - Set controls: Decay=0.5, Damp=0.5, Mix=0.5
   - Play a test signal (10 seconds) or record silence with microphone input
   - Let the AU settle (5 seconds of output audio)
   - **Capture state snapshot:** Note the audio output (peak level, tail behavior)

2. **Change to 48 kHz and verify identical continuation**
   - In AUM: Project → Project Settings → Sample Rate → 48000 Hz
   - AUM may show a reconnection dialog; confirm
   - Play the same test signal for 5 seconds
   - **Verify:** Output continues smoothly; no clicks, pops, or state resets
   - **Capture:** Audio output at 48 kHz (peak level, tail behavior)

3. **Return to 44.1 kHz**
   - Change rate back to 44.1 kHz
   - Play test signal
   - **Verify:** Output at 44.1 kHz matches original baseline

4. **Attempt unsupported rate (96 kHz)**
   - In AUM: Project → Project Settings → Sample Rate → 96000 Hz
   - AUM should show an error or the AU should disconnect
   - **Record outcome:** Error message, returned BOOL, NSError code (if visible in AUM logs)
   - **Verify:** AU rejects the rate; controls remain at prior state (Decay=0.5, Damp=0.5, Mix=0.5)

5. **Attempt unsupported config (mono output)**
   - If AUM allows per-AU output format selection, try mono
   - **Record outcome:** Rejection behavior
   - AU should default to stereo or reject; state preserved either way

**Gate:** Smooth continuation across rate changes; rejection without fallback for invalid rates.

---

## HT-4: Input Error Injection & Missing Input

**Purpose:** Verify AU handles missing input and pull errors gracefully.

**Procedure:**

1. **Baseline with valid input (5 seconds)**
   - Input: Live microphone or test signal
   - Play normal audio through Aetherfield AU
   - **Capture:** Output peak level, confirm non-silence

2. **Disconnect/mute input**
   - In AUM: Tap input source → Disable or mute
   - Continue playback (5 seconds, don't stop the engine)
   - **Verify:** AU still renders (tail from prior input)
   - **Record:** Output level, any artifacts or clicks

3. **Reconnect input**
   - Re-enable input
   - Resume playback (5 seconds)
   - **Verify:** AU resumes processing without gaps or crashes
   - **Record:** Seamless reconnection, output continuity

4. **Observe AU counter (if visible in diagnostics)**
   - Some hosts show per-AU counters or metrics
   - **Record:** Any fault/error counter state before/after missing input

**Gate:** No crash, no invalid audio output (non-finite samples), no blocking on missing input.

**Note:** True pull-error injection (callback returns error code) may not be possible via AUM UI alone. If AUM does not support this, record as "unmeasured via host UI" — a controlled test harness would be needed for full HT-4 coverage.

---

## HT-6: Host Reset Request & Self-Recovery

**Purpose:** Verify AU handles explicit host reset and internal fault recovery.

**Procedure:**

1. **Baseline (10 seconds of normal operation)**
   - Input: Microphone or test signal
   - Play through AU
   - Let it settle (5 seconds)
   - **Capture:** Output level, tail behavior

2. **Trigger host reset (if available in AUM)**
   - In AUM: Tap Aetherfield AU settings/info
   - Look for a "Reset" button or "Panic" option
   - If present: Tap it while audio is playing
   - **Record:** Whether reset was available, any visual feedback
   - **Capture:** Audio output immediately after reset (should mute to silence, then resume)

3. **Continue playback post-reset**
   - Keep the engine running after reset
   - Continue playback (5 seconds)
   - **Verify:** Output resumes from exact silence, no clicks or pops
   - **Verify:** Cumulative AU counter (if visible) increments correctly

4. **Self-recovery test (optional, if you can inject NaN/Inf)**
   - Play a very high-amplitude signal to AU input (try to overflow)
   - Immediately drop to silence (zero input)
   - **Record:** Does AU recover? Output should transition from non-finite/clipped to clean silence
   - **Note:** This may not be possible via AUM UI; skip if unavailable

**Gate:** Reset consumes host request (no stale reset state), exact silence after reset, smooth recovery to normal processing.

---

## Observation & Results Form

After completing HT-2, HT-4, HT-6 on AUM:

### HT-2 Results

| Aspect | 44.1 kHz | 48 kHz | 96 kHz (reject) | Mono (reject) |
|--------|----------|--------|---|---|
| Rate accepted? | ✓ | ✓ | ✗ Expected | ✗ Expected |
| State preserved? | N/A | ✓ Smooth | ✓ Rejected | ✓ Rejected |
| Output continuity? | N/A | ✓ No clicks | N/A | N/A |
| Control values (D/Da/M) | 0.5/0.5/0.5 | 0.5/0.5/0.5 | Unchanged | Unchanged |

**Outcome:** [ ] PASS / [ ] FAIL / [ ] PARTIAL / [ ] UNMEASURED  
**Notes:**

---

### HT-4 Results

| Aspect | Valid Input | Disconnected | Reconnected |
|--------|---|---|---|
| Output present? | ✓ | ✓ Tail | ✓ Resumed |
| Crash? | ✗ | ✗ | ✗ |
| Non-finite output? | ✗ | ✗ | ✗ |
| Seamless reconnect? | N/A | N/A | ✓ |

**Pull-error injection:** [ ] Tested via AUM / [ ] Not supported / [ ] Unmeasured

**Outcome:** [ ] PASS / [ ] FAIL / [ ] PARTIAL / [ ] UNMEASURED  
**Notes:**

---

### HT-6 Results

| Aspect | Baseline | Post-Reset | Resume |
|---|---|---|---|
| Reset available in AUM? | [ ] Yes / [ ] No | N/A | N/A |
| Output pre-reset | [Level: ___] | Muted to 0 | [ ] Clicked / [ ] Clean |
| Cumulative counter | [Value: ___] | — | [Incremented: Y/N] |
| Self-recovery tested? | [ ] Yes / [ ] No | N/A | N/A |

**Outcome:** [ ] PASS / [ ] FAIL / [ ] PARTIAL / [ ] UNMEASURED  
**Notes:**

---

## Reference Audio Signals (Optional)

If you want to use a known test signal instead of live microphone input:

- **Impulse:** Single 1.0 sample at t=0, then silence (reveals early reflections)
- **White noise:** ~0.1 amplitude noise for 10 seconds (reveals decay, tail behavior)
- **Sine sweep:** 100–1000 Hz sweep to check frequency response (advanced)

AUM's input signal generator or external audio app (e.g., Apple Podcasts playing test tone) can supply these.

---

## Troubleshooting

| Issue | Solution |
|---|---|
| AUM can't find Aetherfield AU | Verify AU is installed (check `pluginkit -m`). Restart AUM. |
| Rate change disconnects AU | Normal on some hosts; verify it reconnects and state is preserved. |
| Input muting doesn't work | Try disconnecting the input source cable instead, or disable input bus. |
| Reset button not visible | AUM may not expose this; try stopping/restarting transport in AUM. |
| Output is silent | Check AUM output volume, speaker/headphone connection, AU mute state. |

---

## Next Steps After Task 2

Upon completion, report results here with:
1. Filled observation form (above)
2. Any unexpected behavior or failures
3. Whether each HT-2/4/6 subcategory passed/failed/unmeasured
4. Audio observations (clicks, pops, latency, tail behavior)

I will then:
1. Document findings in testing.md
2. Close Task 2 or flag blockers
3. Proceed to Task 3 (bypass/automation)
