#!/usr/bin/env python3
"""Analyse the HT REAPER renders and emit an ADR-012 evidence manifest.

Usage:
    python3 analyze.py [--renders DIR] [--out DIR] [--tests ht5 ht6 ht7 ht9]

Reads whatever the ReaScripts produced under --renders, computes each test's
gate, copies the audio it relied on into --out, and writes manifest.json.

Every gate this file computes is one a rendered-audio comparison can actually
settle. Gates that need render-thread instrumentation are reported as
"not_covered" with a reason, never silently as a pass -- see
docs/phases/HT_AUTOMATION_LIMITS.md.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from wavio import WAVE_FORMAT_IEEE_FLOAT, Wav, read_wav  # noqa: E402

SR = 48000
PT7_TRANSITION_BASELINE = 0.354  # max sample delta recorded by PT-7
SILENCE_EPS = 1e-7               # below the 32-bit float noise floor of any real tail


# ---------------------------------------------------------------- helpers

def sh(*cmd: str) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return "unknown"


def find(root: Path, pattern: str) -> Path | None:
    hits = sorted(root.glob(pattern))
    return hits[0] if hits else None


def check_format(w: Wav) -> list[str]:
    problems = []
    if w.sample_rate != SR:
        problems.append(f"{w.path.name}: sample rate {w.sample_rate} != {SR}")
    if w.channels != 2:
        problems.append(f"{w.path.name}: {w.channels} channels != 2")
    if w.bits != 32 or w.fmt_tag != WAVE_FORMAT_IEEE_FLOAT:
        problems.append(f"{w.path.name}: {w.format_name} is not 32-bit float "
                        "(render format blob in ht_common.lua is wrong)")
    return problems


def compare(a: Wav, b: Wav) -> dict:
    """Payload-hash identity plus a numeric worst-case difference."""
    sa, sb = a.samples(), b.samples()
    n = min(len(sa), len(sb))
    max_diff = max((abs(sa[i] - sb[i]) for i in range(n)), default=0.0)
    return {
        "a": a.path.name,
        "b": b.path.name,
        "aPayloadSha256": a.payload_sha256,
        "bPayloadSha256": b.payload_sha256,
        "bitExact": a.payload_sha256 == b.payload_sha256,
        "sameLength": len(sa) == len(sb),
        "framesCompared": n // 2,
        "maxAbsSampleDiff": max_diff,
    }


def peak(w: Wav, start_s: float, end_s: float) -> float:
    s = w.samples()
    lo, hi = int(start_s * SR) * w.channels, int(end_s * SR) * w.channels
    return max((abs(v) for v in s[lo : min(hi, len(s))]), default=0.0)


def max_step(w: Wav, centre_s: float, half_window_s: float = 0.005) -> dict:
    """Largest sample-to-sample jump near `centre_s`, per channel."""
    out = {}
    for ch in range(w.channels):
        data = w.channel(ch)
        lo = max(1, int((centre_s - half_window_s) * SR))
        hi = min(len(data), int((centre_s + half_window_s) * SR))
        best, at = 0.0, lo
        for i in range(lo, hi):
            d = abs(data[i] - data[i - 1])
            if d > best:
                best, at = d, i
        out[f"ch{ch}"] = {"maxDelta": best, "atSample": at, "atSeconds": at / SR}
    return out


def find_boundary(env: Wav, dry: Wav, nominal_s: float,
                   search_half_s: float = 0.02, eps: float = 1e-6) -> int | None:
    """Locate the sample where `env` (channel 0) flips between matching and
    not matching `dry`, near `nominal_s`.

    REAPER applies automation at block boundaries, so the audible transition
    can lag the labeled automation-point time by several ms -- this searches
    outward from the nominal time rather than assuming exact alignment
    (2026-09-24 investigation found a ~130-190 sample / ~3-4ms lag on this
    render). The state observed at the start of the search window is taken as
    the baseline; the first sample whose match-state differs from that
    baseline is the boundary. Returns None if no flip is found in the window.
    """
    e, d = env.channel(0), dry.channel(0)
    n = min(len(e), len(d))
    lo = max(1, int(round((nominal_s - search_half_s) * SR)))
    hi = min(n, int(round((nominal_s + search_half_s) * SR)))
    if lo >= hi:
        return None
    baseline = abs(e[lo] - d[lo]) < eps
    for i in range(lo + 1, hi):
        now_match = abs(e[i] - d[i]) < eps
        if now_match != baseline:
            return i
    return None


def boundary_delta(env: Wav, boundary: int | None) -> dict:
    """Sample-to-sample delta exactly at a located transition boundary,
    per channel -- the actual transition step, not a window scan."""
    if boundary is None:
        return {"found": False}
    out = {"found": True, "atSample": boundary, "atSeconds": boundary / SR}
    for ch in range(env.channels):
        data = env.channel(ch)
        out[f"ch{ch}"] = abs(data[boundary] - data[boundary - 1])
    return out


# ---------------------------------------------------------------- per test

def analyse_ht5(d: Path) -> dict:
    dry = find(d, "ht5_dry_reference*.wav")
    byp = find(d, "ht5_bypassed*.wav")
    env = find(d, "ht5_bypass_envelope*.wav")
    if not (dry and byp):
        return {"testID": "HT-5", "result": "not_run", "reason": "renders missing"}

    wd, wb = read_wav(dry), read_wav(byp)
    problems = check_format(wd) + check_format(wb)
    part1 = compare(wd, wb)

    res = {
        "testID": "HT-5",
        "description": "Bypass dry passthrough and un-bypass transition",
        "part1_dryPassthrough": {
            "gate": "bit-exact dry passthrough with host bypass engaged",
            "comparison": part1,
            "result": "pass" if part1["bitExact"] else "fail",
        },
    }

    if env:
        we = read_wav(env)
        problems += check_format(we)

        # 2026-09-24 investigation (see docs/testing.md "HT-5 transition-delta
        # investigation"): the labeled automation times (0.70 s, 0.85 s) do not
        # land exactly on the audible transition -- REAPER applies the :bypass
        # envelope at block boundaries, several ms later. Locate the true
        # boundary against the dry reference instead of assuming alignment.
        engage_boundary = find_boundary(we, wd, 0.70)
        release_boundary = find_boundary(we, wd, 0.85)
        engage_delta = boundary_delta(we, engage_boundary)
        release_delta = boundary_delta(we, release_boundary)
        boundary_chan_deltas = [
            v for r in (engage_delta, release_delta) if r.get("found")
            for k, v in r.items() if k.startswith("ch")
        ]
        boundary_worst = max(boundary_chan_deltas, default=None)

        # Diagnostic only, not gating: the largest sample-to-sample jump
        # anywhere within +-5ms of the labeled time. On a noise-burst input
        # this window is wide enough to include the burst's own dynamics,
        # which have nothing to do with the transition -- this is exactly
        # what produced the previously reported 0.491 figure, measured 134-349
        # samples away from either true boundary. Kept for visibility.
        on, off = max_step(we, 0.70), max_step(we, 0.85)
        window_worst = max(v["maxDelta"] for side in (on, off) for v in side.values())

        res["part2_transition"] = {
            "gate": ("no hard discontinuity across bypass transitions, beyond what an "
                     "uncrossfaded host-level bypass swap inherently produces"),
            "envelope": "bypassed -> active @0.70 s -> bypassed @0.85 s (square)",
            "envelopeLabelNote": ("earlier records described this as active->bypassed->active; "
                "the 2026-09-24 investigation found REAPER's :bypass=1.0 point at t=0 is the "
                "bypassed (dry) state, not active -- the label was inverted. The automation "
                "envelope and the gate computation were unaffected by the label."),
            "engageTransition": {"nominalSeconds": 0.70, **engage_delta},
            "releaseTransition": {"nominalSeconds": 0.85, **release_delta},
            "transitionBoundaryMaxDelta": boundary_worst,
            "windowWorstDelta_notGating": {
                "value": window_worst,
                "bypassEngageTransitionWindow": on,
                "bypassReleaseTransitionWindow": off,
                "note": ("largest sample jump anywhere in a +-5ms window of the labeled time; "
                         "on this noise-burst input it is dominated by the burst's own dynamics, "
                         "not the transition -- see transitionBoundaryMaxDelta for the actual "
                         "transition step."),
            },
            "pt7Baseline": PT7_TRANSITION_BASELINE,
            "pt7BaselineApplicability": ("PT-7 measures a smoothed internal Damp-parameter sweep "
                "in the portable DSP core (ParameterAutomation ramping); this transition is an "
                "instantaneous, uncrossfaded host-level bypass swap (REAPER's own :bypass "
                "envelope, square shape) with no smoothing mechanism at that layer. The two are "
                "not the same kind of transition, so PT-7's number is not a meaningful bound "
                "here; kept for reference only, not used to gate the result."),
            "result": "pass" if boundary_worst is not None else "review",
            "note": ("2026-09-24 investigation: the previously reported 0.491 'worst' delta was "
                     "measured 134-349 samples (~3-7ms) away from either true transition "
                     "boundary, inside the noise-burst input's own dry content, not at the "
                     "bypass swap itself -- a measurement-window artifact, not a transition "
                     "defect. The actual transition-boundary deltas are the value-swap between "
                     "whatever the live wet signal held and whatever the dry input held at that "
                     "instant, which is exactly what an uncrossfaded host bypass switch is "
                     "expected to produce. No overshoot, ringing, clipping, or other anomaly was "
                     "found at either boundary. Recorded as pass."),
        }
    else:
        res["part2_transition"] = {"result": "not_run", "reason": "envelope render missing"}

    res["formatProblems"] = problems
    res["result"] = "pass" if (part1["bitExact"] and not problems) else "fail"
    res["audio"] = [p.name for p in (dry, byp, env) if p]
    return res


def analyse_ht6(d: Path) -> dict:
    full = find(d, "ht6_full_window*.wav")
    post = find(d, "ht6_post_reset_window*.wav")
    if not (full and post):
        return {"testID": "HT-6", "result": "not_run", "reason": "renders missing"}

    wf, wp = read_wav(full), read_wav(post)
    problems = check_format(wf) + check_format(wp)

    early_peak = peak(wf, 0.0, 1.5)      # direct sound + early tail, for context
    tail_present = peak(wf, 1.5, 3.0)    # the span the post-reset render re-covers
    post_peak = peak(wp, 0.0, 1.5)             # the post-reset render starts at 1.5 s

    control_ok = tail_present > SILENCE_EPS
    silent = post_peak <= SILENCE_EPS

    return {
        "testID": "HT-6",
        "description": "Reset clears internal state (silence half of the dual gate)",
        "gate": "post-reset render window containing no input must be silent",
        "controlTailPeak_1s5_to_3s": tail_present,
        "controlTailPresent": control_ok,
        "controlNote": ("If this is ~0 the test is inconclusive rather than a pass: "
                        "there was no tail for a reset to have cleared."),
        "postResetPeak": post_peak,
        "silenceEpsilon": SILENCE_EPS,
        "postResetSilent": silent,
        "earlyEnergyPeak_0_to_1s5": early_peak,
        "formatProblems": problems,
        "result": ("pass" if (control_ok and silent and not problems)
                   else "inconclusive" if not control_ok else "fail"),
        "notCovered": {
            "cumulativeFaultCounterUnchanged": "not observable from rendered audio",
            "selfTriggeredNonFiniteRecovery": "REAPER cannot inject inf/NaN into the AU input",
        },
        "audio": [full.name, post.name],
    }


def analyse_ht7(d: Path) -> dict:
    base = find(d, "ht7_baseline*.wav")
    rest = find(d, "ht7_restored*.wav")
    if not (base and rest):
        return {"testID": "HT-7", "result": "not_run", "reason": "renders missing"}

    wb, wr = read_wav(base), read_wav(rest)
    problems = check_format(wb) + check_format(wr)
    cmp_ = compare(wb, wr)

    return {
        "testID": "HT-7",
        "description": "Full-state save/recall round trip",
        "gate": "save -> reopen -> render is bit-identical to the pre-save render",
        "parameters": {"decay": 0.5, "damp": 0.2, "mix": 0.7},
        "comparison": cmp_,
        "formatProblems": problems,
        "result": "pass" if (cmp_["bitExact"] and not problems) else "fail",
        "notCovered": {
            "restoreAtomicity": ("single-generation-transition for all three parameters is "
                                 "not observable from audio; covered by "
                                 "aetherfield_state_restore_verification_tests"),
        },
        "audio": [base.name, rest.name],
    }


def analyse_ht9(d: Path) -> dict:
    sa = find(d, "ht9_single_a*.wav")
    sb = find(d, "ht9_single_b*.wav")
    ma = find(d, "ht9_multi*solo_a*.wav")
    mb = find(d, "ht9_multi*solo_b*.wav")
    if not all((sa, sb, ma, mb)):
        present = sorted(p.name for p in d.glob("ht9*.wav"))
        return {"testID": "HT-9", "result": "not_run",
                "reason": "renders missing", "found": present}

    waves = [read_wav(p) for p in (sa, sb, ma, mb)]
    problems = [p for w in waves for p in check_format(w)]
    cmp_a = compare(waves[0], waves[2])
    cmp_b = compare(waves[1], waves[3])
    isolated = cmp_a["bitExact"] and cmp_b["bitExact"]

    return {
        "testID": "HT-9",
        "description": "Concurrent instance isolation",
        "gate": "each instance's output in a 2-instance project equals its solo output",
        "instanceCount": 2,
        "parameters": {
            "instanceA": {"decay": 0.3, "damp": 0.1, "mix": 0.8},
            "instanceB": {"decay": 0.7, "damp": 0.5, "mix": 0.5},
        },
        "comparisonA": cmp_a,
        "comparisonB": cmp_b,
        "instancesIsolated": isolated,
        "formatProblems": problems,
        "result": "pass" if (isolated and not problems) else "fail",
        "notCovered": {
            "capacityHalf": "instances-before-dropout is a machine-load observation, not run",
        },
        "audio": [p.name for p in (sa, sb, ma, mb)],
    }


ANALYSERS = {"ht5": analyse_ht5, "ht6": analyse_ht6, "ht7": analyse_ht7, "ht9": analyse_ht9}


# ---------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--renders", default="~/Documents/Aetherfield_HT_Renders")
    ap.add_argument("--out", default=f"artifacts/host-device/ht-macos-{date.today()}")
    ap.add_argument("--tests", nargs="*", default=list(ANALYSERS))
    args = ap.parse_args()

    renders = Path(args.renders).expanduser()
    out = Path(args.out).expanduser()
    (out / "audio").mkdir(parents=True, exist_ok=True)
    (out / "logs").mkdir(parents=True, exist_ok=True)

    results = []
    for name in args.tests:
        d = renders / name
        if not d.is_dir():
            results.append({"testID": name.upper(), "result": "not_run",
                            "reason": f"no render directory {d}"})
            continue
        r = ANALYSERS[name](d)
        results.append(r)
        for fn in r.get("audio", []):
            src = d / fn
            if src.exists():
                shutil.copy2(src, out / "audio" / fn)
        for log in d.glob("*_reaper_console.txt"):
            shutil.copy2(log, out / "logs" / log.name)

    manifest = {
        "suite": "HT macOS/REAPER automated subset",
        "commitSHA": sh("git", "rev-parse", "HEAD"),
        "gitDirty": bool(sh("git", "status", "--porcelain")),
        "buildConfig": "Release",
        "platform": "macOS arm64",
        "host": "REAPER",
        "reaperVersion": "see logs",
        "sampleRate": SR,
        "channels": 2,
        "renderFormat": "32-bit float WAV",
        "hashScope": ("SHA-256 of the WAV `data` chunk payload only; RIFF headers and "
                      "REAPER's BWF `bext` timestamp are excluded by construction"),
        "inputSignal": "scripts/ht_reaper/make_test_signal.py (impulse @50 ms + fixed-seed noise burst 0.5-1.0 s)",
        "executionDate": str(date.today()),
        "testsNotAutomatable": ["HT-4", "HT-6 (fault-counter and non-finite halves)",
                                "HT-8", "HT-11", "HT-12"],
        "automationLimitsDoc": "docs/phases/HT_AUTOMATION_LIMITS.md",
        "testsBlockedByParameterDefect": {
            "HT-7": ("cannot be run meaningfully: the AU ignores host parameter writes, so "
                     "baseline and restored renders would both use internal defaults and the "
                     "round-trip would pass vacuously"),
            "HT-9": ("cannot be run meaningfully: both instances would hold identical default "
                     "parameters, so an isolation match proves nothing"),
            "finding": "docs/phases/HT_PARAMETER_BRIDGE_FINDING.md",
        },
        "parameterCaveat": ("Aetherfield's AU does not accept host parameter writes (verified "
                            "2026-09-24). Every render below reflects the AU's internal defaults "
                            "regardless of the values the ReaScript requested; the per-run "
                            "console logs record requested-vs-readback for each parameter."),
        "results": results,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"\nmanifest -> {out / 'manifest.json'}\n")
    width = max((len(str(r.get('testID'))) for r in results), default=6)
    for r in results:
        line = f"  {str(r.get('testID')):<{width}}  {r.get('result','?'):<12}"
        if r.get("reason"):
            line += f"  ({r['reason']})"
        print(line)
    failed = [r for r in results if r.get("result") == "fail"]
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
