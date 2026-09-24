# HT REAPER harness

Automates the macOS/REAPER HT gates that a rendered audio comparison can actually
decide: **HT-5**, **HT-6 (silence half)**, **HT-7** and **HT-9 (isolation half)**.

Read `docs/phases/HT_AUTOMATION_LIMITS.md` first — it records why HT-4, HT-8 and
the remaining halves of HT-6/HT-7/HT-9 are deliberately *not* here.

## Layout

| File | Role |
|---|---|
| `make_test_signal.py` | generates the deterministic input WAV (impulse + fixed-seed noise burst) |
| `ht_common.lua` | shared ReaScript helpers: new project tab, AU insert, exact params, deterministic render |
| `ht5_bypass.lua` `ht6_reset.lua` `ht7_state.lua` `ht9_multi.lua` | one ReaScript per test; each only renders |
| `wavio.py` | dependency-free WAV reader |
| `analyze.py` | computes every gate, writes `manifest.json` |
| `selftest.py` | proves `analyze.py` reaches the right verdict on synthetic renders; needs neither REAPER nor the AU |
| `bridge.py` | drives a running REAPER through the MCP bridge's file mailbox (no MCP client needed) |
| `COWORK_PROMPT.md` | a self-contained task prompt for running this suite in an autonomous agent session |

Separating render from analysis is deliberate: the ReaScripts make no pass/fail
judgement, so a verdict can always be recomputed from the stored audio.

## Prerequisites

```bash
# AU present (note: NOT the old ~/Library/Audio/Plug-Ins/Components path --
# that legacy .component bundle was deleted as the HT-10 crash cause)
ls -d /Applications/AetherfieldHostMacOS.app/Contents/PlugIns/AetherfieldAUExtensionMacOS.appex

# portable core green
cmake --build build/Release -j4 && (cd build/Release && ctest)
```

In REAPER, "Aetherfield: Reverb" must be scanned (Options → Preferences →
Plug-ins → Audio Units → Re-scan).

## Run

```bash
# 1. input signal
python3 scripts/ht_reaper/make_test_signal.py \
    ~/Documents/Aetherfield_HT_Renders/input/ht_test_signal.wav

# 2. renders -- in REAPER, run each script from Actions -> Show action list ->
#    Load ReaScript, or via the REAPER MCP bridge's script_run.
#    Each script opens its OWN project tab and never touches the open project.
#      ht5_bypass.lua   ht6_reset.lua   ht7_state.lua   ht9_multi.lua
#    Driving it directly (REAPER must be running with reaper_mcp_bridge.lua):
python3 scripts/ht_reaper/bridge.py ping
python3 scripts/ht_reaper/bridge.py run scripts/ht_reaper/ht5_bypass.lua

# 3. verdicts + evidence manifest
python3 scripts/ht_reaper/analyze.py \
    --renders ~/Documents/Aetherfield_HT_Renders \
    --out artifacts/host-device/ht-macos-2026-09-24
```

`analyze.py` exits non-zero if any gate fails. Run `selftest.py` after changing it.

## Design notes

**Exact parameters.** HT-10 could only mouse-drag the generic AU slider and
landed on 0.499 instead of 0.500. These scripts use
`TrackFX_SetParamNormalized` and assert the read-back, so every render has exact,
recorded parameter values.

**Payload hashing.** HT-10 also lost a render round to REAPER's BWF `bext` chunk,
which embeds a wall-clock timestamp and made byte-identical audio hash
differently. `analyze.py` hashes the WAV `data` chunk only, so that class of
false negative cannot recur and "Write BWF metadata" no longer has to be off.

**Format assertion.** Renders must be 48 kHz / stereo / 32-bit float. If
REAPER's render-format blob in `ht_common.lua` is wrong, `analyze.py` fails the
affected test rather than comparing quantised audio.

**HT-6 control.** The reset test renders a control window first and reports
`inconclusive`, not `pass`, if no tail was present for the reset to have cleared.


## Status (2026-09-24)

`ht5_bypass.lua` and `ht6_reset.lua` have been run: **HT-5 PASS**, **HT-6 PASS** on
the silence half.

`ht7_state.lua` and `ht9_multi.lua` are written and ready but **must not be run
yet** — the AU currently ignores host parameter writes, so both would pass
vacuously. See `docs/phases/HT_PARAMETER_BRIDGE_FINDING.md`. The scripts are kept
so they can run unchanged once that is resolved.

To run the suite in an autonomous agent session, use `COWORK_PROMPT.md`.
