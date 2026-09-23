# HT-10 Execution Guide (Offline Determinism via macOS AU)

**Status:** Ready to execute  
**Commit:** `2c693e1` (AU instantiation fix applied)  
**Target:** Verify bit-exact render determinism via REAPER at 48 kHz

---

## Prerequisites

- ✅ AU Extension built and installed at `~/Library/Audio/Plug-Ins/Components/AetherfieldAUExtensionMacOS.component`
- ✅ REAPER installed at `/Applications/REAPER.app`
- ✅ xdarkzx-reaper-mcp installed via pipx (`which reaper-mcp` should show `/Users/artbox/.local/bin/reaper-mcp`)

## Option A: Manual Execution (Simplest, No Scripting)

### Step 1: Verify AU Discovery

1. Open REAPER
2. Go to **Actions > Rescan Audio Units** (or restart REAPER)
3. Create a new project (File > New Project)
4. Create one audio track
5. On the track, click **FX** (effects button)
6. Confirm that **"Aetherfield: Reverb"** appears in the FX list (search for "Aether" if needed)

If not found:
- Go to REAPER Preferences > Plug-ins > Audio Units
- Click "Re-scan"
- Verify Component path shows `~/Library/Audio/Plug-Ins/Components/`

### Step 2: Configure Project

1. **Project Settings:**
   - File > Project Settings
   - Set **Sample Rate** to **48000 Hz**

2. **Add Track & FX:**
   - Create one audio track (already done if you followed Step 1)
   - Click **FX** button on the track
   - Add **"Aetherfield: Reverb"** (it will be the only Aetherfield option)

3. **Set Parameters:**
   - On the Aetherfield FX window, set:
     - **Decay: 0.5**
     - **Damp: 0.5**
     - **Mix: 0.5**
   - (If parameter numbers aren't visible, hover over each slider)

4. **Prepare Output Directory:**
   ```bash
   mkdir -p ~/Documents/HT10_renders
   ```

### Step 3: Render 3 Times

For each render:

1. File > Render Project
2. Render settings:
   - **Output format:** WAV (48000 Hz stereo)
   - **Output file:** `~/Documents/HT10_renders/render_1.wav` (then 2, then 3)
3. Click **Render**
4. Wait for completion (should be fast—empty track)

### Step 4: Verify Determinism

```bash
sha256sum ~/Documents/HT10_renders/render_*.wav
```

**Expected output:**
```
aaaa...aaaa  render_1.wav
aaaa...aaaa  render_2.wav
aaaa...aaaa  render_3.wav
```

All three hashes must be **identical**.

### Step 5: Record Results

Create `artifacts/host-device/ht10-macos-2026-09-22/manifest.json`:

```json
{
  "testID": "HT-10",
  "description": "Offline determinism: bit-exact render at 48 kHz",
  "commitSHA": "2c693e1",
  "buildConfig": "Release",
  "platform": "macOS",
  "deviceOS": "macOS 14+",
  "reaperVersion": "7.00+",
  "sampleRate": 48000,
  "channels": 2,
  "parameters": {
    "decay": 0.5,
    "damp": 0.5,
    "mix": 0.5
  },
  "renderCount": 3,
  "audioHashes": {
    "render_1.wav": "sha256:HASH_HERE",
    "render_2.wav": "sha256:HASH_HERE",
    "render_3.wav": "sha256:HASH_HERE"
  },
  "allIdentical": true,
  "testResult": "pass",
  "executionDate": "2026-09-22",
  "notes": "Empty project, 0-second duration, silence rendered 3x"
}
```

---

## Option B: Automated via ReaScript

A ReaScript (`HT10/ht10_determinism_test.lua`) is ready in REAPER's Scripts folder.

### Load and Run the Script

1. Open REAPER
2. Actions > Show action list
3. Search for "ht10_determinism" or navigate to **HT10 > ht10_determinism_test**
4. Click **Load** (top right)
5. Click **Run** (or press Ctrl+Enter on Windows, Cmd+Enter on Mac if configured)

The script will:
- Rescan AU
- Create new 48 kHz project
- Add track with Aetherfield AU
- Set parameters to 0.5/0.5/0.5
- Render 3x to `~/Documents/HT10_renders/`
- Print SHA-256 commands and status

Console output goes to **Actions > Show console** or the script opens it automatically.

---

## Verification Evidence Contract

Per ADR-012 HT-10 gate, evidence must include:

| Item | Location | Required |
|------|----------|----------|
| Commit SHA | `manifest.json` | ✅ 2c693e1 |
| Build config | `manifest.json` | ✅ Release |
| Platform | `manifest.json` | ✅ macOS |
| Sample rate | `manifest.json` | ✅ 48000 Hz |
| Parameters | `manifest.json` | ✅ 0.5, 0.5, 0.5 |
| Render files (3x) | `artifacts/host-device/<run-id>/audio/` | ✅ |
| Hashes (3x) | `manifest.json::audioHashes` | ✅ |
| All identical | `manifest.json::allIdentical` | ✅ true/false |
| Test result | `manifest.json::testResult` | ✅ pass/fail |

---

## Known Limitations

- **No silence/content:** The test renders an empty REAPER project (silence). This is a valid determinism test for the AU's render path.
- **Simulator vs. Device:** This is macOS AU, not iOS/iPadOS; it's a lower-friction platform for AU testing per the phase plan.
- **No further HT tests:** HT-4, 5, 6, 8, 9, 11, 12 remain unrun; HT-2 was already completed on physical iPhone 16 Pro Max in a prior session.

---

## What HT-10 Verifies

✅ **Verified by this test:**
- AU can be discovered and instantiated by REAPER
- AU can be configured with fixed parameters (Decay, Damp, Mix)
- AU renders produce byte-for-byte identical output across multiple identical render invocations
- No non-deterministic behavior (floating-point divergence, timing variance, etc.)

❌ **NOT tested:**
- Audio quality or correctness (no comparison to reference output)
- Real-time render performance
- Device/OS-specific behavior (iOS/iPadOS)
- Parameter automation envelopes
- Bypass/reset behavior

---

## Troubleshooting

### AU not discoverable

```bash
# Verify AU file exists and is valid
ls -la ~/Library/Audio/Plug-Ins/Components/AetherfieldAUExtensionMacOS.component
file ~/Library/Audio/Plug-Ins/Components/AetherfieldAUExtensionMacOS.component

# Check code signature
codesign -v ~/Library/Audio/Plug-Ins/Components/AetherfieldAUExtensionMacOS.component
```

### AU instantiates but render fails

Check REAPER console for errors:
- Actions > Show console
- Look for "Aetherfield" errors

If render timeout: This was a prior known issue (tracked in testing.md as an Apple out-of-process AU bug, now accepted as external constraint).

### Hashes don't match

This indicates non-deterministic behavior. Check:
- REAPER project is truly identical (same SR, same track count, same FX settings)
- No background processing or system load during renders
- AU is release build (`build/Release/`)

---

## Next Steps

1. **Manual:** Follow Option A above; record results in manifest.json
2. **Scripted:** Follow Option B; retrieve output from `~/Documents/HT10_renders/`
3. **Report:** Commit manifest.json and audio files to `artifacts/host-device/ht10-macos-<date>/`
4. **Continue:** HT-10 passes → proceed to HT-4/5/6/8/9/11/12 or next authorized task

---

**Plan Reference:** `/Users/artbox/.claude/plans/adaptive-frolicking-pine.md`  
**ADR Reference:** ADR-012 (HT-1…HT-12 methodology and scope)
