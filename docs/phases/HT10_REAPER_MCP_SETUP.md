# HT-10 (Offline Determinism) — REAPER MCP Implementation Guide

**Status:** Ready for next session execution  
**Target:** Verify offline render determinism (bit-exact) via REAPER MCP automated testing

## Quick Start

This document guides setting up and running HT-10 (offline determinism) tests using xDarkzx/Reaper-MCP.

### Prerequisites

- **Reaper** installed at `/Applications/REAPER.app`
- **Python 3.10+** with pip
- **macOS AU built**: `platform/apple/build/Release/AetherfieldAUExtensionMacOS.appex`

### Installation (One-Time)

```bash
# Install the REAPER MCP server
pip install xdarkzx-reaper-mcp

# Register with Claude Code (add to ~/.claude.json)
# {
#   "mcpServers": {
#     "reaper": {
#       "command": "reaper-mcp"
#     }
#   }
# }
```

### AU Discovery Setup

The AU must be discoverable to REAPER without sudo:

```bash
# Copy AU to user-level plugin directory (no sudo required)
mkdir -p ~/Library/Audio/Plug-Ins/Components
cp -r platform/apple/build/Release/AetherfieldAUExtensionMacOS.appex \
      ~/Library/Audio/Plug-Ins/Components/

# In REAPER: Actions > Rescan Audio Units
# Or restart REAPER to auto-scan
```

### Verify AU is Discoverable

Via Claude Code MCP, call:
```
fx_list_installed
```

Confirm a result containing: `"Aetherfield: Reverb"`

## HT-10 Test Procedure

### Test Setup (via MCP)

1. Create a fresh REAPER project at 48 kHz (default when creating a new project)
2. Create one audio track
3. Insert Aetherfield AU by name: `fx_add` with `fx_name: "Aetherfield: Reverb"`
4. Set parameters (0=Decay, 1=Damp, 2=Mix):
   - `TrackFX_SetParam(tr, 0, 0, 0.5)` — Decay=0.5
   - `TrackFX_SetParam(tr, 0, 1, 0.5)` — Damp=0.5
   - `TrackFX_SetParam(tr, 0, 2, 0.5)` — Mix=0.5

### Render Three Times

For each render:
1. Call `project_export_audio()` with:
   - `render_dir`: path to output
   - `render_pattern`: filename (e.g., `render_1.wav`, `render_2.wav`, `render_3.wav`)
2. Capture the exported file path

### Compare Outputs

```bash
# Compute SHA-256 of each render
sha256sum render_1.wav render_2.wav render_3.wav

# All three should be identical
```

### Expected Result

✅ **PASS**: All three SHA-256 hashes identical  
❌ **FAIL**: Hashes differ

## Evidence Contract (ADR-012)

Create `artifacts/host-device/<run-id>/manifest.json`:

```json
{
  "commitSHA": "3a20d7d...",
  "buildConfig": "Release",
  "deviceOS": "macOS",
  "reaperVersion": "7.00+",
  "sampleRate": 48000,
  "parameters": {"decay": 0.5, "damp": 0.5, "mix": 0.5},
  "renderCount": 3,
  "audioHashes": {
    "render_1.wav": "sha256:...",
    "render_2.wav": "sha256:...",
    "render_3.wav": "sha256:..."
  },
  "allIdentical": true,
  "testResult": "pass"
}
```

Subdirectories:
- `logs/` — MCP server logs, render output logs
- `audio/` — the three rendered .wav files
- `projects/` — saved REAPER .rpp if automated; or notes if manual

## Reference: MCP Tools Used

| Tool | Purpose |
|------|---------|
| `fx_list_installed` | Verify AU is discoverable |
| `fx_add` | Insert AU by name on track |
| `TrackFX_SetParam` | Set Decay/Damp/Mix parameters |
| `project_export_audio` | Render project to .wav |

## Known Limitations

- **No sample-rate setter**: REAPER projects default to 48 kHz; setting is manual in REAPER UI or via a `project_main_action(42379)` workaround (exact command ID TBD).
- **No automation envelopes**: Dense parameter automation (HT-8) not automated via this MCP; manual Reaper automation or separate scripting required.

## Next Steps

1. Install `xdarkzx-reaper-mcp` via pip
2. Register MCP in `~/.claude.json`
3. Copy AU to `~/Library/Audio/Plug-Ins/Components/`
4. Call `fx_list_installed` to confirm AU is found
5. Execute render-3x-and-compare procedure
6. Record results in `testing.md` and `artifacts/host-device/<run-id>/manifest.json`

---

**Plan Reference**: `/Users/artbox/.claude/plans/adaptive-frolicking-pine.md`  
**Session**: 2026-09-22, Task 2–5 golden path (HT-2 + HT-10)
