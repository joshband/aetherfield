# Task 2 Focused Execution — HT-2, HT-4, HT-6

**Date:** 2026-09-22  
**Device:** iPhone 16 Pro Max (iOS 27.0)  
**Host:** AUM v5.0+  
**Aetherfield AU:** Discovered as Reverb/Josh  

---

## Pre-Flight (2 min)

- [ ] iPhone unlocked, AUM launch-ready
- [ ] Speaker/headphone output connected and working
- [ ] Microphone or audio input source ready
- [ ] Open Terminal on Mac; ready to capture any AU logs

**Start time:** _____ UTC

---

## HT-2: Rate Negotiation (5 min)

**In AUM:**

1. Create → New Session → Confirm audio routing is active
2. Insert Aetherfield AU (Reverb category, manufacturer Josh)
3. Set controls: Decay 0.5, Damp 0.5, Mix 0.5
4. **Baseline at 44.1 kHz:** 
   - Project Settings → Sample Rate: 44100 Hz
   - Play microphone input or white noise for 10 sec
   - **Listen:** Clear reverb tail, no glitches
   - Note audio level: L=___ R=___

5. **Change to 48 kHz:**
   - Project Settings → Sample Rate: 48000 Hz
   - Keep audio playing (don't stop)
   - **Listen:** Smooth continuation, no clicks/pops
   - Note audio level: L=___ R=___

6. **Return to 44.1 kHz:**
   - Project Settings → Sample Rate: 44100 Hz
   - **Listen:** Returns to original behavior

7. **Try 96 kHz (expect rejection):**
   - Project Settings → Sample Rate: 96000 Hz
   - **Observe:** AU disconnects, refuses rate, or AUM shows error
   - Note error: _________________
   - **Verify:** Controls still show Decay 0.5, Damp 0.5, Mix 0.5

**HT-2 Outcome:** [ ] PASS (smooth rate changes, rejects 96k) / [ ] FAIL (glitches, doesn't reject) / [ ] PARTIAL (__)

---

## HT-4: Missing Input (5 min)

**In AUM:**

1. Set input to microphone; play audio through AU for 5 sec
2. **Mute input** (tap input source → mute, or disable bus):
   - Keep AU/engine running (don't stop transport)
   - Continue for 5 sec
   - **Listen:** Tail continues, no artifacts
   - Note: Tail audible? [Y/N] Audio level drops to: ____

3. **Unmute input:**
   - Re-enable input
   - Continue for 5 sec
   - **Listen:** Resumes without gaps, clicks, or artifacts
   - Note: Smooth recovery? [Y/N]

4. **Crash test:** Tap AU settings, look for "Reset" or diagnostic info
   - No crash during any of above steps? [ ] Y / [ ] N

**HT-4 Outcome:** [ ] PASS (no crash, tail continues, clean reconnect) / [ ] FAIL (__) / [ ] PARTIAL (__)

---

## HT-6: Reset & Recovery (3 min)

**In AUM:**

1. Play normal audio (microphone) for 5 sec
   - Note baseline output level: ____

2. **Locate reset/panic:**
   - Tap Aetherfield AU (long-press or settings icon)
   - Look for "Reset" button or "Panic" option
   - **If found:** Tap it while audio is playing
   - **If not found:** Skip to step 4; record "reset not exposed in AUM"

3. **Observe post-reset:**
   - Output should mute to silence briefly, then resume
   - Continue playback for 5 sec
   - **Listen:** No clicks on reset, resumes from clean silence
   - Note: Clean reset? [Y/N] Clicks on resume? [Y/N]

4. **Self-recovery (optional):**
   - Try playing a very loud signal (max volume on microphone)
   - Drop to silence immediately
   - AU should recover, output tail should be normal
   - Note: Can generate extreme input? [Y/N] AU recovers cleanly? [Y/N]

**HT-6 Outcome:** [ ] PASS (reset works, no clicks, clean recovery) / [ ] SKIP (reset not exposed) / [ ] FAIL (__) / [ ] PARTIAL (__)

---

## Summary (1 min)

After completing all three:

| Test | Outcome | Notes |
|---|---|---|
| HT-2 (Rate changes) | [ ] PASS / [ ] FAIL | Rate changes: Smooth? [Y/N] Rejects 96k? [Y/N] |
| HT-4 (Missing input) | [ ] PASS / [ ] FAIL | Tail continues? [Y/N] Clean reconnect? [Y/N] |
| HT-6 (Reset) | [ ] PASS / [ ] SKIP / [ ] FAIL | Reset available? [Y/N] Clean reset? [Y/N] |

**End time:** _____ UTC  
**Total duration:** _____ min

---

## Report Back With

1. Completion time
2. Summary table (above) filled in
3. Any crashes, glitches, or unexpected behavior
4. Which tests passed/failed/skipped
5. Subjective audio observations (clicks, smoothness, latency perception)

**Do not modify any project files or make audio recordings** — just observe and report.
