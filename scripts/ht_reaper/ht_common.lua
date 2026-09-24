-- Shared helpers for the Aetherfield HT REAPER harness.
--
-- Every HT script opens a NEW PROJECT TAB and works only there, so running the
-- harness never touches whatever project the user already has open.
--
-- Load with:  local H = dofile(script_dir .. "ht_common.lua")

local H = {}

-- REAPER accepts several spellings depending on how the FX browser and the AU
-- cache name the plugin. reaper-auplugins_arm64.ini registers it as
-- "Aetherfield: Reverb"; the FX browser shows "AU: Reverb (Aetherfield)".
-- H.add_au tries each in turn so a naming difference is not a failed run.
H.AU_NAMES = {
  "AU: Reverb (Aetherfield)",
  "AUi: Reverb (Aetherfield)",
  "Aetherfield: Reverb",
  "Reverb (Aetherfield)",
}
H.AU_NAME       = H.AU_NAMES[1]
H.SR            = 48000
H.RENDER_ROOT   = os.getenv("HOME") .. "/Documents/Aetherfield_HT_Renders"
H.INPUT_WAV     = H.RENDER_ROOT .. "/input/ht_test_signal.wav"

-- REAPER render-format cfg blobs (base64 of the WAV sink config).
-- 32-bit float keeps full precision so bit-exactness gates are not masked by
-- quantisation. analyze.py asserts the real format of every file it reads, so a
-- wrong blob here fails loudly rather than silently degrading the evidence.
H.FMT_WAV_F32 = "ZXZhdyAAAAA="
H.FMT_WAV_I24 = "ZXZhdxgAAAA="

----------------------------------------------------------------------
-- logging
----------------------------------------------------------------------

local log_lines = {}

function H.log(fmt, ...)
  local msg = select("#", ...) > 0 and string.format(fmt, ...) or fmt
  log_lines[#log_lines + 1] = msg
  reaper.ShowConsoleMsg(msg .. "\n")
end

function H.write_log(path)
  local f = assert(io.open(path, "w"))
  f:write(table.concat(log_lines, "\n"), "\n")
  f:close()
end

function H.fail(fmt, ...)
  local msg = select("#", ...) > 0 and string.format(fmt, ...) or fmt
  H.log("FATAL: " .. msg)
  error(msg, 0)
end

----------------------------------------------------------------------
-- filesystem
----------------------------------------------------------------------

function H.mkdir(path)
  os.execute("mkdir -p '" .. path .. "'")
  return path
end

function H.file_exists(path)
  local f = io.open(path, "rb")
  if f then f:close() return true end
  return false
end

----------------------------------------------------------------------
-- project setup
----------------------------------------------------------------------

--- Open a clean project tab and set its sample rate. Returns nothing; all
--- subsequent calls operate on project 0 (the newly active tab).
function H.new_tab(sr)
  reaper.Main_OnCommand(40859, 0) -- New project tab
  sr = sr or H.SR
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE", sr, true)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
  H.log("new project tab @ %d Hz", sr)
end

function H.add_track(name)
  local idx = reaper.CountTracks(0)
  reaper.InsertTrackAtIndex(idx, false)
  local tr = reaper.GetTrack(0, idx)
  if name then
    reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", name, true)
  end
  return tr
end

--- Insert H.INPUT_WAV at position 0 on `tr`.
function H.add_input_item(tr)
  if not H.file_exists(H.INPUT_WAV) then
    H.fail("test signal missing: %s (run scripts/ht_reaper/make_test_signal.py)", H.INPUT_WAV)
  end
  reaper.SetOnlyTrackSelected(tr)
  reaper.SetEditCurPos(0, false, false)
  reaper.InsertMedia(H.INPUT_WAV, 0)
  H.log("inserted input item on '%s'", H.track_name(tr))
end

function H.track_name(tr)
  local _, n = reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", "", false)
  return n
end

----------------------------------------------------------------------
-- FX
----------------------------------------------------------------------

function H.add_au(tr)
  for _, name in ipairs(H.AU_NAMES) do
    local fx = reaper.TrackFX_AddByName(tr, name, false, -1)
    if fx >= 0 then
      reaper.TrackFX_Show(tr, fx, 2)  -- hide floating window
      reaper.TrackFX_Show(tr, fx, 0)  -- hide chain window
      H.log("inserted '%s' on '%s' as fx %d", name, H.track_name(tr), fx)
      return fx
    end
  end
  H.fail("could not insert the Aetherfield AU under any known name "
         .. "(%s) -- is it installed and scanned in REAPER?",
         table.concat(H.AU_NAMES, ", "))
end

--- Resolve a parameter index by (case-insensitive, substring) name.
function H.param_index(tr, fx, want)
  local n = reaper.TrackFX_GetNumParams(tr, fx)
  for i = 0, n - 1 do
    local _, pname = reaper.TrackFX_GetParamName(tr, fx, i, "")
    if pname:lower():find(want:lower(), 1, true) then return i end
  end
  H.fail("no parameter matching '%s' (plugin exposes %d params)", want, n)
end

--- Set Decay/Damp/Mix to exact normalised values.
--- This is the key improvement over HT-10, where the generic AU slider could
--- only be mouse-dragged and landed on 0.499 instead of 0.500.
function H.set_params(tr, fx, decay, damp, mix)
  local want = { Decay = decay, Damp = damp, Mix = mix }
  local got = {}
  for name, value in pairs(want) do
    local i = H.param_index(tr, fx, name)
    reaper.TrackFX_SetParamNormalized(tr, fx, i, value)
    got[name] = reaper.TrackFX_GetParamNormalized(tr, fx, i)
  end
  local verified = true
  for name, value in pairs(want) do
    local delta = math.abs(got[name] - value)
    H.log("param %-5s requested=%.9f readback=%.9f delta=%.9f%s",
          name, value, got[name], delta, delta > 1e-6 and "   <-- MISMATCH" or "")
    if delta > 1e-6 then verified = false end
  end

  -- A mismatch is recorded, not fatal. Aetherfield's AU currently reports 0.0 for
  -- every host parameter read and ignores host writes entirely (verified against
  -- an Apple in-process AU control, which round-trips correctly) -- see
  -- docs/phases/HT_PARAMETER_BRIDGE_FINDING.md. Failing here would hide the tests
  -- that are still meaningful under the AU's own defaults; instead every affected
  -- render carries "parametersVerified": false into the manifest.
  if not verified then
    H.log("WARNING: host parameter read-back did not match. Renders from this "
          .. "project reflect the AU's internal defaults, NOT the values above.")
  end
  got.verified = verified
  return got
end

function H.bypass_param(tr, fx)
  local i = reaper.TrackFX_GetParamFromIdent(tr, fx, ":bypass")
  if not i or i < 0 then H.fail("plugin exposes no :bypass parameter") end
  return i
end

----------------------------------------------------------------------
-- render
----------------------------------------------------------------------

--- Configure deterministic render settings for a custom time range.
-- mode: "master" (default) or "stems"
function H.render_config(dir, basename, start_s, end_s, mode)
  H.mkdir(dir)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", dir, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", basename, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", H.FMT_WAV_F32, true)

  reaper.GetSetProjectInfo(0, "RENDER_SRATE",     H.SR, true)
  reaper.GetSetProjectInfo(0, "RENDER_CHANNELS",  2, true)
  reaper.GetSetProjectInfo(0, "RENDER_SETTINGS",  mode == "stems" and 3 or 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)  -- custom time range
  reaper.GetSetProjectInfo(0, "RENDER_STARTPOS",  start_s, true)
  reaper.GetSetProjectInfo(0, "RENDER_ENDPOS",    end_s, true)
  reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG",  0, true)
  reaper.GetSetProjectInfo(0, "RENDER_TAILMS",    0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ADDTOPROJ", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_NORMALIZE", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_DITHER",    0, true)
end

--- Render and block until the expected file appears. Returns its path.
-- Uses action 42230 (render with most recent settings, auto-close window) so no
-- GUI interaction and no synthetic keystrokes are involved.
function H.render(dir, basename, start_s, end_s, mode)
  H.render_config(dir, basename, start_s, end_s, mode)
  local expected = dir .. "/" .. basename .. ".wav"
  os.remove(expected)

  H.log("render -> %s  [%.3f .. %.3f s, %s]", expected, start_s, end_s, mode or "master")
  reaper.Main_OnCommand(42230, 0)

  local deadline = os.time() + 60
  while not H.file_exists(expected) and os.time() < deadline do
    -- Main_OnCommand(42230) is blocking, but guard against async completion.
  end
  if not H.file_exists(expected) then
    H.fail("render produced no file at %s", expected)
  end
  return expected
end

return H
