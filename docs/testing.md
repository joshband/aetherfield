# Testing and evidence

## IMPLEMENTED: Phase 0 host loop

Use the commands in [README.md](../README.md). CMake builds `aetherfield_dsp`, `aetherfield_dsp_tests`, and `aetherfield_render`; CTest runs `aetherfield_dsp_tests`. Tests use explicit failure returns so Release builds do not compile out assertions.

The gain tests cover known attenuation, unity, mute, zero-length/null processing, requested buffer extent, repeated output and finite results within the documented input contract. No feedback, automation, realtime deadline, mobile CPU or host integration claims follow from these tests.

The renderer's fixed integer-derived input avoids randomness, clocks and transcendental-library variation. WAV output is little-endian PCM. The 16-frame fixture is intentionally only a structural proof; listening, RT60, spectral analysis and the musical evaluation corpus are deferred until DSP makes them useful.

## Inspect the generated artifact

`file` identifies the container. For an independent structural and decoded-sample check, Python 3's standard library suffices (Python is optional inspection tooling, not a build dependency):

```sh
python3 - <<'PY'
import hashlib
import pathlib
import struct
import wave

p = pathlib.Path('artifacts/phase0-gain.wav')
data = p.read_bytes()
assert len(data) == 76
assert data[:4] == b'RIFF' and data[8:12] == b'WAVE'
assert struct.unpack_from('<I', data, 4)[0] == len(data) - 8
with wave.open(str(p), 'rb') as w:
    assert (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()) == (1, 2, 48000, 16)
    assert w.getcomptype() == 'NONE'
    samples = struct.unpack('<16h', w.readframes(16))
expected = (-15000, -12000, -9000, -6000, -3000, -1500, 0, 1500,
            3000, 6000, 9000, 12000, 15000, 9000, 3000, 0)
assert samples == expected
assert max(abs(x) for x in samples) == 15000
print('PASS: 76 bytes, 16 PCM frames, all samples match; peak=15000/32768')
print('SHA256:', hashlib.sha256(data).hexdigest())
PY
```

For deterministic replay, render a second file and compare:

```sh
./build/aetherfield_render artifacts/phase0-gain-repeat.wav
cmp artifacts/phase0-gain.wav artifacts/phase0-gain-repeat.wav
```

The expected samples are derived from the fixture's specified 0.5 gain, not copied from a passing output. Do not replace expected samples or later accepted reference baselines just to make a failing change pass.

## Independent acceptance evidence

Luna independently verified Phase 0 on 2026-09-16 using CMake 4.2.2 and Apple Clang 21.0.0, macOS arm64. A new build directory was used, with the process-local Command Line Tools environment described in README:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -S . -B build/luna-clean -DCMAKE_BUILD_TYPE=Release
cmake --build build/luna-clean --parallel
ctest --test-dir build/luna-clean --output-on-failure
./build/luna-clean/aetherfield_render artifacts/luna-phase0-gain.wav
./build/luna-clean/aetherfield_render artifacts/luna-phase0-gain-repeat.wav
cmp artifacts/luna-phase0-gain.wav artifacts/luna-phase0-gain-repeat.wav
```

Results:

- Fresh configure/build succeeded. All three targets enable `-Wall -Wextra -Wpedantic -Werror` on Clang/GNU; the build passed with those flags.
- CTest: **1/1 passed**, with the Release-safe checks described above. Local detailed log: `build/luna-clean/Testing/Temporary/LastTest.log` (generated and ignored).
- Renderer succeeded; **76 bytes, PCM16, mono, 48 kHz, 16 frames**. All 16 decoded samples matched the expected values; peak was `15000/32768` (approximately 0.4578 full scale). The exact Python snippet above also passed.
- Independent and repeated renders matched byte-for-byte. SHA256: `444a1cc51a7636703277a763074fadf095d2c3f600d2f471ffce4ec386436ed0`.
- Invalid invocation with no output argument exited 2; a missing output directory exited 1; the `/dev/full` probe exited 1. The stream is explicitly closed before its final error check.
- Source and documentation inspection confirmed the minimal 13-file project, absence of reverb/platform/UI implementation, and Mermaid's explicitly deferred AUv3/UI links.
- The charter contains 47 sections and 1,648 lines. Terra additionally compared its 34,836 bytes against the original supplied charter, excluding only the trailing directory-creation instruction: byte equality passed.

The charter retains its original two-space Markdown hard break on line 3. Git's default whitespace check flags that preserved formatting; checks on all other project files pass.

## Limits and future work

Current host verification is macOS arm64 only. Platform-independent code is a design property; cross-platform builds, iOS compilation, simulator/device hosting, sample-rate lifecycle and render-thread instrumentation remain unverified. The DSP function is auditable scalar arithmetic without allocation or I/O; no instrumented allocation or realtime deadline test has been run.

Phase 1 should specify meaningful stability and parameter tests before the reverb implementation begins. Future validation combines deterministic inputs, bounds/NaN/Inf stress checks, decay/stereo/spectral measurements where relevant, and listening. Measurements cannot establish sonic quality alone.
