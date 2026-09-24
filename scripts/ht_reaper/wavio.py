"""Minimal dependency-free WAV reader for the HT harness.

Only what the analysis needs: walk the RIFF chunks, decode the `data` payload to
floats, and expose the raw payload bytes separately.

Hashing the `data` payload rather than the whole file is deliberate. HT-10 lost a
full render round to REAPER's BWF `bext` chunk, which embeds a wall-clock
origination timestamp and made byte-identical audio hash differently. Payload
hashing makes that class of false negative impossible.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

WAVE_FORMAT_PCM = 1
WAVE_FORMAT_IEEE_FLOAT = 3
WAVE_FORMAT_EXTENSIBLE = 0xFFFE


@dataclass
class Wav:
    path: Path
    sample_rate: int
    channels: int
    bits: int
    fmt_tag: int
    data: bytes

    @property
    def frames(self) -> int:
        return len(self.data) // (self.channels * self.bits // 8)

    @property
    def payload_sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()

    @property
    def format_name(self) -> str:
        kind = "float" if self.fmt_tag == WAVE_FORMAT_IEEE_FLOAT else "int"
        return f"{self.bits}-bit {kind}, {self.channels}ch, {self.sample_rate} Hz"

    def samples(self) -> list[float]:
        """Interleaved samples normalised to [-1, 1]."""
        if self.fmt_tag == WAVE_FORMAT_IEEE_FLOAT and self.bits == 32:
            n = len(self.data) // 4
            return list(struct.unpack(f"<{n}f", self.data[: n * 4]))
        if self.fmt_tag == WAVE_FORMAT_IEEE_FLOAT and self.bits == 64:
            n = len(self.data) // 8
            return list(struct.unpack(f"<{n}d", self.data[: n * 8]))
        if self.bits == 16:
            n = len(self.data) // 2
            raw = struct.unpack(f"<{n}h", self.data[: n * 2])
            return [v / 32768.0 for v in raw]
        if self.bits == 24:
            out = []
            for i in range(0, len(self.data) - 2, 3):
                v = int.from_bytes(self.data[i : i + 3], "little", signed=True)
                out.append(v / 8388608.0)
            return out
        if self.bits == 32:  # 32-bit int PCM
            n = len(self.data) // 4
            raw = struct.unpack(f"<{n}i", self.data[: n * 4])
            return [v / 2147483648.0 for v in raw]
        raise ValueError(f"{self.path.name}: unsupported format tag={self.fmt_tag} bits={self.bits}")

    def channel(self, index: int) -> list[float]:
        return self.samples()[index :: self.channels]


def read_wav(path: str | Path) -> Wav:
    path = Path(path)
    blob = path.read_bytes()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")

    sample_rate = channels = bits = fmt_tag = None
    data = None

    pos = 12
    while pos + 8 <= len(blob):
        cid = blob[pos : pos + 4]
        (size,) = struct.unpack("<I", blob[pos + 4 : pos + 8])
        body = blob[pos + 8 : pos + 8 + size]

        if cid == b"fmt ":
            fmt_tag, channels, sample_rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if fmt_tag == WAVE_FORMAT_EXTENSIBLE and len(body) >= 40:
                # The real tag lives in the first two bytes of the GUID.
                (fmt_tag,) = struct.unpack("<H", body[24:26])
        elif cid == b"data":
            data = body

        pos += 8 + size + (size & 1)  # chunks are word-aligned

    if data is None or sample_rate is None:
        raise ValueError(f"{path}: missing fmt or data chunk")
    return Wav(path, sample_rate, channels, bits, fmt_tag, data)
