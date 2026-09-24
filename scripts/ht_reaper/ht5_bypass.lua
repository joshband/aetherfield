-- HT-5: Bypass and tail behaviour (ADR-012 HT-5, ADR-009).
--
-- Part 1 (gated): with bypass engaged the AU must pass the dry signal through
--                 bit-exactly. Rendered against a no-AU reference.
-- Part 2 (gated): un-bypassing must not produce a hard discontinuity. Rendered
--                 with a square bypass envelope; analyze.py locates the true
--                 transition sample against the dry reference (REAPER applies
--                 automation at block boundaries, several ms after the
--                 labeled point) and measures the delta exactly there.
--
-- Renders land in ~/Documents/Aetherfield_HT_Renders/ht5/.

local dir = debug.getinfo(1, "S").source:match("^@?(.*[/\\])")
local H = dofile(dir .. "ht_common.lua")

local OUT = H.RENDER_ROOT .. "/ht5"
local T0, T1 = 0.0, 3.0

reaper.ClearConsole()
H.log("=== HT-5 bypass / tail ===")
H.mkdir(OUT)

----------------------------------------------------------------------
-- Part 1a: dry reference, no AU on the track at all
----------------------------------------------------------------------
H.new_tab()
local tr = H.add_track("ht5")
H.add_input_item(tr)
H.render(OUT, "ht5_dry_reference", T0, T1)

----------------------------------------------------------------------
-- Part 1b: AU inserted and host-bypassed
----------------------------------------------------------------------
local fx = H.add_au(tr)
H.set_params(tr, fx, 0.5, 0.0, 1.0)
reaper.TrackFX_SetEnabled(tr, fx, false)
H.log("host bypass engaged: TrackFX_GetEnabled=%s", tostring(reaper.TrackFX_GetEnabled(tr, fx)))
H.render(OUT, "ht5_bypassed", T0, T1)

----------------------------------------------------------------------
-- Part 2: square bypass envelope across the noise burst (0.5 .. 1.0 s)
--   bypassed -> active at 0.70 s -> bypassed again at 0.85 s
--   (REAPER's :bypass parameter is 1.0 = bypassed, 0.0 = active; verified
--   2026-09-24 by comparing the render against the dry reference sample by
--   sample -- an earlier version of this comment had the labels inverted)
-- Both transitions sit inside the burst so there is real signal on each side.
----------------------------------------------------------------------
reaper.TrackFX_SetEnabled(tr, fx, true)

local bp = H.bypass_param(tr, fx)
local env = reaper.GetFXEnvelope(tr, fx, bp, true)
if not env then H.fail("could not create bypass envelope") end

reaper.DeleteEnvelopePointRange(env, -1, 1e9)
-- shape 1 = square, so the transitions are instantaneous and unambiguous.
reaper.InsertEnvelopePoint(env, 0.00, 1.0, 1, 0, false, true)
reaper.InsertEnvelopePoint(env, 0.70, 0.0, 1, 0, false, true)
reaper.InsertEnvelopePoint(env, 0.85, 1.0, 1, 0, false, true)
reaper.Envelope_SortPoints(env)
H.log("bypass envelope: bypassed(1.0) @0.00 -> active(0.0) @0.70 -> bypassed(1.0) @0.85 (square)")
H.log("bypass param index=%d, current normalised value=%.6f", bp,
      reaper.TrackFX_GetParamNormalized(tr, fx, bp))

H.render(OUT, "ht5_bypass_envelope", T0, T1)

H.write_log(OUT .. "/ht5_reaper_console.txt")
H.log("=== HT-5 renders complete ===")
