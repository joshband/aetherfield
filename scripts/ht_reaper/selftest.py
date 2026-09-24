#!/usr/bin/env python3
"""Self-test for analyze.py: synthesise renders with known answers and assert
the analyser reaches the right verdict. Runs without REAPER or the AU."""

import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).parent
SR = 48000


def wav(path, frames, bits=32, float_fmt=True):
    path.parent.mkdir(parents=True, exist_ok=True)
    if float_fmt:
        data = b"".join(struct.pack("<ff", l, r) for l, r in frames)
        tag, bits = 3, 32
    else:
        data = b"".join(struct.pack("<hh", int(l * 32767), int(r * 32767)) for l, r in frames)
        tag, bits = 1, 16
    ba = 2 * bits // 8
    fmt = struct.pack("<HHIIHHH", tag, 2, SR, SR * ba, ba, bits, 0)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
    # A BWF-style chunk carrying a "timestamp": payload hashing must ignore it.
    bext = b"bext" + struct.pack("<I", 8) + path.name[:8].ljust(8, "\0").encode()
    body += bext + b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def tone(n, amp=0.3, freq=440.0, phase=0.0):
    return [(amp * math.sin(2 * math.pi * freq * i / SR + phase),) * 2 for i in range(n)]


def silence(n):
    return [(0.0, 0.0)] * n


def build(root: Path, *, break_ht7: bool, break_ht9: bool):
    n3 = int(3.0 * SR)

    # HT-5: bypassed == dry reference (pass). Envelope render with a small step.
    d = root / "ht5"
    base = tone(n3)
    wav(d / "ht5_dry_reference.wav", base)
    wav(d / "ht5_bypassed.wav", base)
    env = list(base)
    at = int(0.85 * SR)
    for i in range(at, n3):  # 0.05 step at the un-bypass point
        env[i] = (env[i][0] + 0.05, env[i][1] + 0.05)
    wav(d / "ht5_bypass_envelope.wav", env)

    # HT-6: full window has a tail past 1.5 s; post-reset window is silent (pass).
    d = root / "ht6"
    full = tone(n3, amp=0.4)
    wav(d / "ht6_full_window.wav", full)
    wav(d / "ht6_post_reset_window.wav", silence(int(1.5 * SR)))

    # HT-7: baseline vs restored.
    d = root / "ht7"
    wav(d / "ht7_baseline.wav", base)
    wav(d / "ht7_restored.wav", tone(n3, amp=0.31) if break_ht7 else base)

    # HT-9: stems, with REAPER's "-<track>" suffix.
    d = root / "ht9"
    a, b = tone(n3, amp=0.2), tone(n3, amp=0.5, freq=330.0)
    wav(d / "ht9_single_a-solo_a.wav", a)
    wav(d / "ht9_single_b-solo_b.wav", b)
    wav(d / "ht9_multi-solo_a.wav", tone(n3, amp=0.2001) if break_ht9 else a)
    wav(d / "ht9_multi-solo_b.wav", b)


def run(root: Path, out: Path) -> dict:
    subprocess.run(
        [sys.executable, str(HERE / "analyze.py"), "--renders", str(root), "--out", str(out)],
        capture_output=True, text=True, cwd=HERE,
    )
    return json.loads((out / "manifest.json").read_text())


def verdicts(m):
    return {r["testID"]: r["result"] for r in m["results"]}


def main():
    failures = []

    def expect(label, got, want):
        ok = got == want
        print(f"  [{'ok' if ok else 'FAIL'}] {label}: got {got!r}, want {want!r}")
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        print("all-good renders:")
        build(tmp / "good", break_ht7=False, break_ht9=False)
        m = run(tmp / "good", tmp / "out_good")
        v = verdicts(m)
        for t in ("HT-5", "HT-6", "HT-7", "HT-9"):
            expect(t, v[t], "pass")
        ht5 = next(r for r in m["results"] if r["testID"] == "HT-5")
        expect("HT-5 transition delta measured",
               round(ht5["part2_transition"]["transitionBoundaryMaxDelta"], 3) >= 0.05, True)
        expect("HT-5 payload hash ignores bext chunk",
               ht5["part1_dryPassthrough"]["comparison"]["bitExact"], True)

        print("\nbroken HT-7 and HT-9:")
        build(tmp / "bad", break_ht7=True, break_ht9=True)
        v = verdicts(run(tmp / "bad", tmp / "out_bad"))
        expect("HT-7", v["HT-7"], "fail")
        expect("HT-9", v["HT-9"], "fail")

        print("\nmissing renders:")
        (tmp / "empty").mkdir()
        v = verdicts(run(tmp / "empty", tmp / "out_empty"))
        for t in ("HT5", "HT6", "HT7", "HT9"):
            expect(t, v[t], "not_run")

        print("\nHT-6 with no tail (must be inconclusive, not pass):")
        build(tmp / "notail", break_ht7=False, break_ht9=False)
        wav(tmp / "notail" / "ht6" / "ht6_full_window.wav", silence(int(3.0 * SR)))
        v = verdicts(run(tmp / "notail", tmp / "out_notail"))
        expect("HT-6", v["HT-6"], "inconclusive")

        print("\nwrong render format (16-bit int) must not pass:")
        build(tmp / "fmt", break_ht7=False, break_ht9=False)
        wav(tmp / "fmt" / "ht7" / "ht7_baseline.wav", tone(int(3.0 * SR)), float_fmt=False)
        wav(tmp / "fmt" / "ht7" / "ht7_restored.wav", tone(int(3.0 * SR)), float_fmt=False)
        v = verdicts(run(tmp / "fmt", tmp / "out_fmt"))
        expect("HT-7", v["HT-7"], "fail")

    print("\n" + ("SELFTEST PASSED" if not failures else f"SELFTEST FAILED: {failures}"))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
