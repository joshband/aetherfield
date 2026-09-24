-- HT-7: full-state save/recall round trip (ADR-012 HT-7, ADR-011).
--
-- Gate covered here: save -> reopen -> render must be bit-identical to the
-- pre-save render. Saving the .RPP drives the AU's getState; reopening it drives
-- setState, the StateSchema validation path and the pre-render atomic snap.
--
-- NOT covered: the restore-atomicity half of HT-7's dual gate (that all three
-- parameters land in a single generation transition) is not observable from
-- rendered audio; it stays covered by aetherfield_state_restore_verification_tests.

local dir = debug.getinfo(1, "S").source:match("^@?(.*[/\\])")
local H = dofile(dir .. "ht_common.lua")

local OUT  = H.RENDER_ROOT .. "/ht7"
local RPP  = OUT .. "/ht7_roundtrip.RPP"
local T0, T1 = 0.0, 3.0

-- Deliberately not 0.5/0.5/0.5: three distinct, non-default values so a restore
-- that silently falls back to defaults cannot masquerade as a pass.
local DECAY, DAMP, MIX = 0.5, 0.2, 0.7

reaper.ClearConsole()
H.log("=== HT-7 state save/recall ===")
H.mkdir(OUT)

----------------------------------------------------------------------
-- Baseline: configure directly, render, then save (triggers getState)
----------------------------------------------------------------------
H.new_tab()
local tr = H.add_track("ht7")
H.add_input_item(tr)
local fx = H.add_au(tr)
H.set_params(tr, fx, DECAY, DAMP, MIX)

H.render(OUT, "ht7_baseline", T0, T1)

os.remove(RPP)
reaper.Main_SaveProjectEx(0, RPP, 0)
if not H.file_exists(RPP) then H.fail("project did not save to %s", RPP) end
H.log("saved project (AU getState) -> %s", RPP)

----------------------------------------------------------------------
-- Restore: fresh tab, reopen the project (triggers setState), render again
----------------------------------------------------------------------
reaper.Main_OnCommand(40859, 0) -- new project tab
reaper.Main_openProject("noprompt:" .. RPP)
H.log("reopened project (AU setState)")

local rtr = reaper.GetTrack(0, 0)
if not rtr then H.fail("restored project has no track 0") end
local rfx = 0
if reaper.TrackFX_GetCount(rtr) < 1 then H.fail("restored project has no FX on track 0") end

-- Read back what the restore actually produced. These are recorded evidence:
-- the audio comparison is the gate, this is the corroborating detail.
for _, name in ipairs({ "Decay", "Damp", "Mix" }) do
  local i = H.param_index(rtr, rfx, name)
  H.log("restored %s = %.9f", name, reaper.TrackFX_GetParamNormalized(rtr, rfx, i))
end

H.render(OUT, "ht7_restored", T0, T1)

H.write_log(OUT .. "/ht7_reaper_console.txt")
H.log("=== HT-7 renders complete ===")
