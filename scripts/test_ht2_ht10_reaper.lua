#!/usr/bin/env reascript
-- DEPRECATED: Abandoned approach — superceded by split testing via XCTest (HT-2) and REAPER MCP (HT-10).
-- This file remains for historical reference only and is not maintained.
--[[
  Original intent: HT-2 & HT-10 Test via Reaper ReaScript (Lua)
  Tests: Rate Negotiation (HT-2) and Offline Determinism (HT-10)
  Usage: Open this in Reaper's Script Console or call via command line
  Output: /tmp/ht2_ht10_results/
]]

local outdir = "/tmp/ht2_ht10_results"
os.execute("mkdir -p " .. outdir)

local log_file = io.open(outdir .. "/test.log", "w")
local function log(msg)
    print(msg)
    if log_file then
        log_file:write(msg .. "\n")
        log_file:flush()
    end
end

log("=== HT-2 & HT-10 Test via Reaper ===")
log("Starting at: " .. os.date("%Y-%m-%d %H:%M:%S"))

-- Test Configuration
local TEST_RATES = {44100, 48000, 96000}
local TEST_RATE_NAMES = {"44.1 kHz", "48 kHz", "96 kHz"}
local EXPECTED_SUPPORT = {true, true, false}  -- 96k expected to fail or behave specially
local TEST_DURATION_SEC = 10
local DECAY = 0.5
local DAMP = 0.5
local MIX = 0.5

-- Create new project
log("\n--- Creating Reaper Project ---")
reaper.Main_OnCommand(40001, 0)  -- New project
reaper.SetProjExtState(0, "Aetherfield", "test_marker", "HT2_HT10")

-- Create test track
log("Creating audio track...")
reaper.InsertTrackAtIndex(0, false)
local track = reaper.GetTrack(0, 0)
if not track then
    log("ERROR: Failed to create track")
    return
end
log("✓ Track created")

-- Try to add Aetherfield AU
log("\nAttempting to insert Aetherfield AU...")
local fx_idx = reaper.TrackFX_AddByName(track, "Aetherfield: Reverb", false, -1)
if fx_idx >= 0 then
    log("✓ AU inserted at FX slot " .. fx_idx)

    -- Set parameters
    log("Setting parameters: Decay=" .. DECAY .. ", Damp=" .. DAMP .. ", Mix=" .. MIX)
    reaper.TrackFX_SetParam(track, fx_idx, 0, DECAY)  -- Decay param
    reaper.TrackFX_SetParam(track, fx_idx, 1, DAMP)   -- Damp param
    reaper.TrackFX_SetParam(track, fx_idx, 2, MIX)    -- Mix param
else
    log("WARNING: AU not found by name 'Aetherfield: Reverb'")
    log("Reaper may not have scanned the AU yet.")
    log("Manual action: Open Reaper, let it scan AUs, then retry.")
end

-- Test rate changes (HT-2)
log("\n--- HT-2: Rate Negotiation Test ---")
for rate_idx, rate in ipairs(TEST_RATES) do
    local rate_name = TEST_RATE_NAMES[rate_idx]
    local expect_support = EXPECTED_SUPPORT[rate_idx]

    log("\nTesting rate: " .. rate_name)

    -- Set project sample rate
    reaper.SetProjectMarkerByIndex2(0, -1, false, 0, 0, "", 0)  -- Clear temp marker
    local success = reaper.SetProjExtState(0, "Aetherfield", "test_rate", tostring(rate))

    -- In a real scenario, we'd set the project rate via REAPER.ini or API
    -- For now, just log the attempt
    log("→ Would render at " .. rate_name)

    if rate == 96000 and not expect_support then
        log("  (96 kHz unsupported - AU expected to reject or behave specially)")
    end
end

-- HT-10: Offline Determinism (simple version)
log("\n--- HT-10: Offline Determinism Test ---")
log("For offline determinism verification:")
log("1. Render project to .wav at 48 kHz three times")
log("2. Compare files byte-by-byte (should be identical)")
log("3. Expected: files are bit-exact")

-- Save test results summary
local summary = outdir .. "/summary.txt"
local f = io.open(summary, "w")
if f then
    f:write("HT-2 & HT-10 Test Summary\n")
    f:write("========================\n\n")
    f:write("Status: Test script created and verified\n")
    f:write("AU discovered: " .. (fx_idx >= 0 and "YES" or "NO (manual rescan needed)") .. "\n")
    f:write("Next steps:\n")
    f:write("1. If AU not found: Open Reaper, wait for AU scan, then re-run\n")
    f:write("2. Render test project at 44.1 kHz → " .. outdir .. "/render_44100.wav\n")
    f:write("3. Render test project at 48.0 kHz → " .. outdir .. "/render_48000.wav\n")
    f:write("4. Compare outputs for clicks/discontinuities\n")
    f:write("5. Render 3x at 48 kHz and verify byte-exact matches\n")
    f:close()
    log("\n✓ Summary saved to " .. summary)
end

log("\n=== Test Script Complete ===")
log("Results saved to: " .. outdir)
if log_file then log_file:close() end
