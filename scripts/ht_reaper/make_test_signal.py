#!/usr/bin/env python3
"""Generate the deterministic HT test signal.

One stereo 48 kHz WAV containing, in order:
  - 0.050 s  impulse (single full-scale sample, L and R)   -> tail / decay probes
  - 0.500 s  broadband noise burst, fixed LCG seed         -> bypass bit-exactness
  - silence to 3.000 s                                     -> reset / silence probes

32-bit float PCM so nothing is quantised away before the AU sees it.
No third-party dependencies: the WAV is written byte by byte.
"""

import struct
import sys
from pathlib import Path

SR = 48000
DURATION_S = 3.0
IMPULSE_AT_S = 0.050
NOISE_START_S = 0.500
NOISE_LEN_S = 0.500
NOISE_PEAK = 0.5


def lcg(seed: int):
    """Deterministic PRNG; identical output on every platform and Python build."""
    state = seed
    while True:
        state = (state * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        yield ((state >> 33) / float(1 << 31)) - 1.0


def build_frames() -> list[tuple[float, float]]:
    total = int(DURATION_S * SR)
    frames = [(0.0, 0.0)] * total

    frames[int(IMPULSE_AT_S * SR)] = (1.0, 1.0)

    rng = lcg(0x5EED_1234)
    start = int(NOISE_START_S * SR)
    for n in range(start, start + int(NOISE_LEN_S * SR)):
        left = next(rng) * NOISE_PEAK
        right = next(rng) * NOISE_PEAK
        frames[n] = (left, right)

    return frames


def write_wav_f32(path: Path, frames: list[tuple[float, float]]) -> None:
    channels, bits = 2, 32
    block_align = channels * bits // 8
    data = b"".join(struct.pack("<ff", l, r) for l, r in frames)

    # WAVE_FORMAT_IEEE_FLOAT (3) with the 18-byte fmt chunk libsndfile/REAPER expect.
    fmt = struct.pack(
        "<HHIIHHH", 3, channels, SR, SR * block_align, block_align, bits, 0
    )
    chunks = b"WAVE"
    chunks += b"fmt " + struct.pack("<I", len(fmt)) + fmt
    chunks += b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", len(chunks)) + chunks)


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "ht_test_signal.wav").expanduser()
    out.parent.mkdir(parents=True, exist_ok=True)
    write_wav_f32(out, build_frames())
    print(f"wrote {out}  ({out.stat().st_size} bytes, {DURATION_S}s @ {SR} Hz stereo f32)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
