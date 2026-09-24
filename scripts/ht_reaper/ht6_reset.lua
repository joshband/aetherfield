-- HT-6 (part 1 only): reset clears internal state (ADR-012 HT-6, DS-B Task 3).
--
-- Gate covered here: after a host reset, with no input in the rendered window,
-- the AU must output silence rather than a carried-over tail.
--
-- Method: one project, two renders.
--   A: full 0.0 .. 3.0 s          -> proves a long tail exists past 1.5 s
--   B: custom range 1.5 .. 3.0 s  -> REAPER resets the AU at render start; the
--                                    only input in this window is silence, so a
--                                    correctly-reset AU must emit silence. Any
--                                    surviving tail energy here is a failure.
--
-- NOT covered: the "cumulative fault counter unchanged across reset" half of
-- HT-6's dual gate, and the self-triggered non-finite-input recovery case.
-- Neither is observable from a REAPER render -- see HT_AUTOMATION_LIMITS.md.

local dir = debug.getinfo(1, "S").source:match("^@?(.*[/\\])")
local H = dofile(dir .. "ht_common.lua")

local OUT = H.RENDER_ROOT .. "/ht6"

reaper.ClearConsole()
H.log("=== HT-6 reset / silence ===")
H.mkdir(OUT)

H.new_tab()
local tr = H.add_track("ht6")
H.add_input_item(tr)
local fx = H.add_au(tr)
-- Long decay, no damping, full wet: maximises the tail we are trying to prove
-- does NOT survive the reset.
H.set_params(tr, fx, 0.8, 0.0, 1.0)

H.render(OUT, "ht6_full_window",       0.0, 3.0)
H.render(OUT, "ht6_post_reset_window", 1.5, 3.0)

H.write_log(OUT .. "/ht6_reaper_console.txt")
H.log("=== HT-6 renders complete ===")
