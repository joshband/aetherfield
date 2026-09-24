# Autonomous agent prompt — run the Aetherfield HT REAPER suite

Paste the block below as the task for an autonomous agent session (Claude Cowork,
a Claude Code background job, or a subagent). It is self-contained: it assumes no
memory of the session that built the harness.

Prerequisites the agent cannot create for itself: a macOS machine with REAPER
installed, the Aetherfield AU built and scanned, and permission to run shell
commands and launch apps. Accessibility permission for System Events is needed
only for the dialog-recovery step.

---

```
You are running the Aetherfield host-acceptance (HT) test suite on macOS through
REAPER. The harness already exists — do not rebuild it. Your job is to execute it,
judge the results honestly, and record evidence.

REPO: /Users/artbox/Documents/Repos/aetherfield
HARNESS: scripts/ht_reaper/  (read its README.md first)

READ FIRST, IN THIS ORDER
  1. scripts/ht_reaper/README.md
  2. docs/phases/HT_AUTOMATION_LIMITS.md      — which gates a render can decide
  3. docs/phases/HT_PARAMETER_BRIDGE_FINDING.md — the open defect blocking HT-7/HT-9
Do not read the whole docs tree. This project is explicit that an accepted ADR or a
written plan does not authorize implementation; you are executing and measuring only.

HARD RULES
  - Do NOT modify src/, platform/, or any AU/DSP source. If a test fails, report it.
    Fixing it is separate work that requires the owner's explicit authorization.
  - Do NOT commit. This project's standing instruction (docs/agent-log.md) is that no
    commit is made without the owner's explicit request. Leave changes in the working
    tree and say what you changed.
  - Preserve unrelated working-tree changes. Expect platform/apple/project.yml,
    platform/apple/Aetherfield.xcodeproj/project.pbxproj and src/auv3/Info.plist to be
    already modified. Leave them alone.
  - Never claim a pass you did not observe in output you actually read.

STEP 1 — PREFLIGHT
  cd /Users/artbox/Documents/Repos/aetherfield
  cmake --build build/Release -j4 && (cd build/Release && ctest)
      Expect 9/9 passed. If not, stop and report.
  python3 scripts/ht_reaper/selftest.py
      Expect SELFTEST PASSED. This validates the analyzer without REAPER or the AU.
  ls -d /Applications/AetherfieldHostMacOS.app/Contents/PlugIns/AetherfieldAUExtensionMacOS.appex
      The AU is HERE. It is NOT in ~/Library/Audio/Plug-Ins/Components/ — a stale
      .component bundle at that path was the cause of an earlier REAPER crash and was
      deleted. If one has reappeared, delete it before going further.
  grep -i aether ~/Library/Application\ Support/REAPER/reaper-auplugins_arm64.ini
      Expect "Aetherfield: Reverb". If absent, rescan AUs in REAPER.
  python3 scripts/ht_reaper/make_test_signal.py \
      ~/Documents/Aetherfield_HT_Renders/input/ht_test_signal.wav

STEP 2 — GET REAPER AND THE BRIDGE UP
  python3 scripts/ht_reaper/bridge.py ping        # expect e.g. 7.80/macOS-arm64
  If that fails:
    - pgrep -x REAPER. If REAPER is not running: `open -a REAPER`, wait ~10s, ping again.
      Launching REAPER re-runs Scripts/__startup.lua, which starts reaper_mcp_bridge.lua
      automatically. You do not need to load anything by hand.
    - Do NOT trust the reaper MCP tools as a liveness signal. The bridge has been alive
      while the MCP server side still reported CONNECTION_REFUSED. bridge.py is the
      authoritative check, and the harness does not need the MCP tools at all.

  IF THE BRIDGE STOPS RESPONDING MID-RUN it is almost always a modal dialog blocking
  REAPER's main thread. Read it and dismiss it:
    osascript -e 'tell application "System Events" to tell process "REAPER" to get {name, subrole} of every window'
    osascript -e 'tell application "System Events" to tell process "REAPER" to get value of every static text of (first window whose subrole is "AXDialog")'
    osascript -e 'tell application "System Events" to tell process "REAPER" to click button "No" of (first window whose subrole is "AXDialog")'
  Read the dialog before clicking. Only click "No" on a "QuerySave unsaved project
  before closing?" dialog for a scratch tab the harness created; never discard the
  owner's own project. `screencapture` is not permitted in this environment, but
  Accessibility queries work.

STEP 3 — RENDER
  Run each script and read its console log afterwards:
    python3 scripts/ht_reaper/bridge.py run scripts/ht_reaper/ht5_bypass.lua
    python3 scripts/ht_reaper/bridge.py run scripts/ht_reaper/ht6_reset.lua
  Each opens its own REAPER project tab and never touches the open project. They
  deliberately do not close their tabs — closing one raises the modal save dialog above.
  Logs: ~/Documents/Aetherfield_HT_Renders/ht{5,6}/ht{5,6}_reaper_console.txt

  HT-7 and HT-9 (ht7_state.lua, ht9_multi.lua) are BLOCKED. Do not run them and
  report a result — see STEP 5. Run them only under the condition in STEP 6.

STEP 4 — ANALYSE
  python3 scripts/ht_reaper/analyze.py \
      --renders ~/Documents/Aetherfield_HT_Renders \
      --out artifacts/host-device/ht-macos-$(date +%F) \
      --tests ht5 ht6
  Exit code is non-zero if any gate failed. Read manifest.json, do not just trust the
  summary line. Sanity-check that the results are not vacuous:
    - HT-5 part 1 should be bitExact with maxAbsSampleDiff 0.0 over ~144000 frames.
    - HT-6 must show controlTailPresent true. If it is false the analyzer reports
      "inconclusive", which is NOT a pass — say so.
    - formatProblems must be empty. Anything there means the render format is wrong
      and the comparison is untrustworthy.
  HT-5 part 2 locates the true bypass-transition sample against the dry reference
  (REAPER applies :bypass automation at block boundaries, a few ms after the labeled
  time -- do not assume the labeled time is the audible transition) and measures the
  delta exactly there (transitionBoundaryMaxDelta). It reports "review" only if no
  boundary is found. PT-7's 0.354 is recorded alongside for reference but is not a
  meaningful bound for this mechanism (PT-7 measures an unrelated smoothed internal
  parameter sweep, not an uncrossfaded host-level bypass swap) -- do not gate on it.
  A 2026-09-24 investigation found the harness's earlier ±5 ms window-scan figure
  (reported as 0.491 that day) was itself a measurement artifact from noise-burst
  input content, not the transition; see docs/testing.md's HT-5 "Correction" note
  before re-deriving conclusions from an old manifest.json.

STEP 5 — CHECK WHETHER THE BLOCKING DEFECT STILL EXISTS
  As of 2026-09-24 the AU ignores host parameter writes and reports 0.0 for every host
  parameter read, while exposing correct names and ranges. Verify whether that still
  holds — do not assume either way. The ht5/ht6 console logs already print
  requested-vs-readback for each parameter; look for lines marked "<-- MISMATCH".

  If mismatches are present: HT-7 and HT-9 remain blocked. Running them would produce
  a vacuous pass (HT-7 would compare two default-parameter renders; HT-9 would compare
  two instances holding identical defaults). Report them as BLOCKED, not as not-run and
  not as passing.

  If the read-backs now match the requested values, the defect has been fixed — go to
  STEP 6.

STEP 6 — ONLY IF PARAMETER READ-BACK NOW WORKS
    python3 scripts/ht_reaper/bridge.py run scripts/ht_reaper/ht7_state.lua
    python3 scripts/ht_reaper/bridge.py run scripts/ht_reaper/ht9_multi.lua
    python3 scripts/ht_reaper/analyze.py \
        --renders ~/Documents/Aetherfield_HT_Renders \
        --out artifacts/host-device/ht-macos-$(date +%F) --tests ht5 ht6 ht7 ht9
  Before reporting HT-7 or HT-9 as a pass, confirm from the console logs that the
  parameters were actually distinct from the AU's defaults (Decay 0.5 / Damp 0.0 /
  Mix 1.0). A pass with default values everywhere is vacuous and must be reported as
  such. For HT-9 also confirm the two stem files genuinely differ from each other.

STEP 7 — REPORT
  Write your results into docs/testing.md and docs/agent-log.md following the format of
  the existing 2026-09-24 entries, and update the HT status lines in docs/start-here.md
  and docs/phases/HT_STATUS_SUMMARY.md if any verdict changed. Where a new result
  contradicts an existing document, record the discrepancy explicitly rather than
  quietly rewriting the old claim — this project requires that.

  In your final message state, in plain terms:
    - each test's verdict and the number behind it
    - anything you marked blocked, inconclusive or review, and why
    - what you did NOT run and why
    - that nothing was committed, and which files you changed

  Do not report HT-4, HT-8, HT-11 or HT-12. HT-4's and HT-8's gates cannot be decided
  by a REAPER render at all, and HT-11/HT-12 need physical iOS devices. If you think
  you have found a way to automate HT-4 or HT-8 against REAPER, re-read
  HT_AUTOMATION_LIMITS.md before acting on it — producing a manifest that looks like
  evidence without exercising the named gate is worse than leaving the test unrun.

IF YOU GET STUCK
  Stop after 2-3 failed attempts at the same thing and report what you tried, what the
  output was, and what you think is needed. Do not loop, and do not start modifying the
  AU or the harness to make a test pass.
```
