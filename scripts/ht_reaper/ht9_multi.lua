-- HT-9 (isolation half): concurrent instances do not influence each other.
--
-- Gate covered here: two AU instances with different parameters, running in the
-- same project at the same time, must each produce exactly the output they
-- produce alone.
--
-- Every render uses stems mode so the single-instance references and the
-- multi-instance stems traverse an identical signal path (track FX -> stem),
-- making the comparison apples-to-apples.
--
-- NOT covered: the recorded "capacity" half (instances before dropout). That is
-- a machine-load observation, not a logical gate -- see HT_AUTOMATION_LIMITS.md.

local dir = debug.getinfo(1, "S").source:match("^@?(.*[/\\])")
local H = dofile(dir .. "ht_common.lua")

local OUT = H.RENDER_ROOT .. "/ht9"
local T0, T1 = 0.0, 3.0

local P1 = { decay = 0.3, damp = 0.1, mix = 0.8 }
local P2 = { decay = 0.7, damp = 0.5, mix = 0.5 }

reaper.ClearConsole()
H.log("=== HT-9 multi-instance isolation ===")
H.mkdir(OUT)

local function single(track_name, p, basename)
  H.new_tab()
  local tr = H.add_track(track_name)
  H.add_input_item(tr)
  local fx = H.add_au(tr)
  H.set_params(tr, fx, p.decay, p.damp, p.mix)
  reaper.SetOnlyTrackSelected(tr)
  H.render(OUT, basename, T0, T1, "stems")
end

-- REAPER appends the track name to the stem pattern, so a single-track stem
-- render writes "<basename>-<track>.wav". Keep the track names distinct.
single("solo_a", P1, "ht9_single_a")
single("solo_b", P2, "ht9_single_b")

----------------------------------------------------------------------
-- Both instances live in one project, rendered in a single pass
----------------------------------------------------------------------
H.new_tab()
local ta = H.add_track("solo_a")
H.add_input_item(ta)
local fa = H.add_au(ta)
H.set_params(ta, fa, P1.decay, P1.damp, P1.mix)

local tb = H.add_track("solo_b")
H.add_input_item(tb)
local fb = H.add_au(tb)
H.set_params(tb, fb, P2.decay, P2.damp, P2.mix)

reaper.Main_OnCommand(40296, 0) -- Track: Select all tracks
H.log("rendering %d tracks as stems in one pass", reaper.CountTracks(0))
H.render(OUT, "ht9_multi", T0, T1, "stems")

H.write_log(OUT .. "/ht9_reaper_console.txt")
H.log("=== HT-9 renders complete ===")
