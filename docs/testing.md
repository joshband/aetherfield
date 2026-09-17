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

Phase 1 prerequisite recheck (2026-09-16): Luna reran the existing unchanged host binaries with the requested Command Line Tools environment; CTest remained 1/1 passing and `artifacts/luna-phase1-check.wav` matched the Phase 0 SHA256 and bytes. `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild -version` returned Xcode 27.0, build 27A266a. The license blocker is resolved; this version check is not an iOS build or AUv3 integration test.

Current host verification is macOS arm64 only. Platform-independent code is a design property; cross-platform builds, iOS compilation, simulator/device hosting, sample-rate lifecycle and render-thread instrumentation remain unverified. The DSP function is auditable scalar arithmetic without allocation or I/O; no instrumented allocation or realtime deadline test has been run.

Phase 1 should specify meaningful stability and parameter tests before the reverb implementation begins. Future validation combines deterministic inputs, bounds/NaN/Inf stress checks, decay/stereo/spectral measurements where relevant, and listening. Measurements cannot establish sonic quality alone.

## Phase 1 architecture-stage verification (2026-09-16)

Luna independently verified the Phase 1 architecture-decision deliverables — ADR-002 (docs/decisions.md), the S1 task plan (docs/phase1-s1-plan.md), and the two cross-reference edits to roadmap.md and dsp-design.md — against the actual files on disk, not against Sol's or Terra's self-reports.

Checked and passed:

- **Scope compliance.** Only the four documents above changed; no file under `src/`, `tests/`, or `tools/`, and no `CMakeLists.txt`, was created or modified. Nothing was committed by Sol or Terra. This is Phase 1's binding exit criterion: an architecture decision and a task plan, no implementation.
- **Cross-reference consistency.** roadmap.md's Phase 1 row 1 and dsp-design.md's "PLANNED: Phase 1 architecture" status line were edited narrowly and now agree with ADR-002.
- **ADR-002 scope guard.** Its explicit exclusions cover pitch/spectral processing, Freeze/Bloom/Texture, stereo strategy, UI, parameter semantics and the AUv3 framework choice. It states, without hedging, that the fixed-network stability proof is limited to a time-invariant/unmodulated system and that no modulation is authorized by this ADR.
- **S1 plan coverage.** Every required-evidence item in the "Delay/lifecycle skeleton" gate below is covered by a named test case in docs/phase1-s1-plan.md, and the plan explicitly does not authorize writing any `.h`/`.cpp`/`CMakeLists.txt` file.
- **Arithmetic spot-check.** The Householder diagonal at N=8 (1 − 2/8 = 0.75) and the Hadamard addition count (8·log₂8 = 24) in ADR-002 are correctly computed.
- **Citation spot-check.** One cited URL (Stanford CCRMA, "Achieving Desired Reverberation Times") was fetched and confirmed to resolve to the claimed content.

No sonic, stability, or performance claim is verified by this pass — none exists yet. This review confirms only that the architecture decision is internally consistent, honestly scoped, and free of unauthorized implementation; it is a documentation and scope audit, not a build/test/render verification like Phase 0's.

## IMPLEMENTED: Phase 1 S1 — delay/lifecycle skeleton (2026-09-16)

Terra implemented `DelayLine` (`src/dsp/DelayLine.h`/`.cpp`) exactly per docs/phase1-s1-plan.md's interface, plus `tests/DelayLineTests.cpp` covering all 9 named cases from the plan's table, and wired a new `aetherfield_dsp_delay_tests` CTest target mirroring the existing pattern (separate executable, same `-Wall -Wextra -Wpedantic -Werror`, own `add_test`).

Terra's own build/test evidence, from a clean build directory:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure   # repeated, to check determinism
```

Results: fresh configure/build succeeded with all warning flags on; `2/2` tests passed (`aetherfield_dsp_tests`, `aetherfield_dsp_delay_tests`); the repeated `ctest` run produced identical pass results.

**Independent verification (Luna, 2026-09-16).** Luna re-read docs/phase1-s1-plan.md and diffed the actual `DelayLine.h`/`.cpp` against its exact interface block; confirmed all 9 named test cases exist and test what the plan's table specifies (including the `operator new`/`operator delete` allocation-instrumentation case); confirmed none of the plan's explicit non-goals were violated (no feedback matrix, no multiple lines, no damping filter, no runtime-adjustable length/pre-delay/modulation/interpolation, no denormal/NaN/Inf mitigation code); and independently reran the full build and test suite from a separate clean build directory, twice, confirming identical passing results before deleting that directory. Luna also confirmed `CMakeLists.txt`'s new test target matches the plan's "Build wiring" section (separate executable, not merged into `aetherfield_dsp_tests`; same warning flags; own CTest registration). All checks passed; no spec deviation was found.

This closes the "Delay/lifecycle skeleton" gate below. No feedback, decay, damping, matrix, stereo, parameter, or sonic-quality claim follows from it — `DelayLine` alone is not a reverb.

## IMPLEMENTED: Phase 1 S2 — fixed late network (2026-09-16)

Terra implemented `FeedbackDelayNetwork` (`src/dsp/FeedbackDelayNetwork.h`/`.cpp`) per docs/phase1-s2-plan.md's interface, plus two small non-breaking additions to `DelayLine` (`peek()`/`push()`, splitting `process()`'s existing behavior into its two independent halves — `process()` itself and all 9 of its S1 tests are unchanged), `tests/FdnTests.cpp` covering one named case per NS-1 through NS-11, and wired a new `aetherfield_dsp_fdn_tests` CTest target mirroring the existing pattern.

**Two design corrections made during implementation, not present in the original plan (see docs/agent-log.md for full detail):** (1) the plan's original capacity design — reserving `round(f_s·t_max)+64` samples per line while using only `mᵢ` of them — is not expressible through `DelayLine`'s actual shipped interface, which has no separate capacity/active-length concept; each line is instead prepared at exactly its own `mᵢ` (ADR-005 (b)'s own named alternative). (2) the plan did not originally expose per-line coefficient accessors that NS-2/NS-3/NS-5 need to check `aᵢ`/`gᵢ`/`mᵢ` directly rather than only through black-box signal behavior; `lineCount()`, `delaySamples()`, `dampingCoefficient()` and `foldedLineGain()` were added. Both corrections are recorded in docs/phase1-s2-plan.md itself, not hidden.

Terra's own build/test evidence, from a clean build directory:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure   # repeated, to check determinism
```

Results: fresh configure/build succeeded with all warning flags on; `3/3` tests passed (`aetherfield_dsp_tests`, `aetherfield_dsp_delay_tests`, `aetherfield_dsp_fdn_tests`); the repeated `ctest` run produced identical pass results. Recorded diagnostic output from the FDN suite: NS-7's peak output magnitude (≈23.6×, at `T60_max`/minimum-damping/full-scale-sine stress) was **identical across all five required block partitions** `{1, 13, 64, 512, 977}`, direct evidence of partition-independence; `nonFiniteCount() == 0` throughout. NS-8 measured time-to-sustained-exact-silence at ≈1,177,358 samples after a full-scale impulse at `T60₀ = 4s`. NS-10's float-vs-independent-double-reference maximum divergence was `6.65e-08`, far inside the stated `1e-3` tolerance. NS-9's cutoff/no-cutoff and `FPCR.FZ` on/off timings were recorded (no pass/fail gate, per ADR-003 (c)) but this pass could not exercise a true no-cutoff code path — see the note below.

**Known scope limits of this test pass, stated rather than hidden:** NS-8's "time-to-first-denormal with the cutoff disabled" sub-measurement and NS-9's "without cutoff" measurement both require a test-only build seam that bypasses ADR-003 (b)'s cutoff, which this implementation does not add; NS-9's "without cutoff" figure is therefore a same-conditions repeat measurement, not a true comparison, and is reported as such rather than presented as the real thing. NS-6's per-band decay-error measurement (using a burst + Goertzel analysis at 8 log-spaced frequencies) is recorded but not gated against a numeric pass/fail threshold, since ADR-003 assigns the JND judgment to a listening-informed review, not a hard-coded percentage; the broadband (damping-bypassed) decay-law check against the exact predicted curve **is** gated, at a 15% tolerance, and passed.

**Independent verification (Luna, 2026-09-16).** Luna independently rebuilt from a separate clean build directory (deleted afterward), confirmed identical pass results across two `ctest` runs, and confirmed the recorded diagnostics (NS-7 peak ≈23.561× identical across all five block partitions with `nonFiniteCount() == 0`; NS-8 time-to-silence 1,177,358 samples; NS-10 divergence 6.64e-08) by rerunning the binary directly. Luna quoted the exact source lines implementing ADR-003 (b)'s denormal cutoff at both, and only, its two named placements (`FeedbackDelayNetwork.cpp`: the damping-state write and the delay-line push), and confirmed the non-finite handling asymmetry (input substituted; internally-arising non-finite values stored as-is, never corrected). Luna independently reasoned through the fast-Hadamard-transform-plus-folded-gain render path against the separately-constructed diagnostic matrix and found them mathematically equivalent. Luna confirmed `DelayLine::process()`'s refactor into `peek()` then `push()` preserves its original behavior and that all 9 S1 tests still pass unmodified. Luna independently re-verified both implementation-time corrections (the capacity-design fix and the four coefficient accessors) against `DelayLine.h`'s actual contract and the NS-2/NS-3/NS-5 requirements, confirming both were real, correctly-diagnosed problems with correct fixes. Scope compliance confirmed via `git diff --stat`/`git status --short`: no parameter-transport, modulation, or diffusion code exists. All checks passed; no deviation found.

This closes the "Fixed late network" gate below for the NS-1…NS-11 cases. No parameter-transport layer, diffusion, stereo strategy, or sonic-quality claim follows from it — `FeedbackDelayNetwork` alone is not a reverb, and its NS-test injection/output-tap convention is explicitly a test-only convention (docs/phase1-s2-plan.md), never a product decision.

## PLANNED validation gates after Phase 1

These gates describe future work. None is an executed reverb test, and none changes the Phase 0 reference WAV.

| Gate | Required evidence | Ownership |
|---|---|---|
| Delay/lifecycle skeleton | Exact impulse positions and wraparound; rejected invalid preparation; repeatable reset; all buffer extents respected; zero-frame calls safe; allocation occurs only during preparation | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S1" above** |
| Fixed late network | Independently computed matrix orthogonality and damping bounds; finite deterministic impulse/silence/noise renders; double-precision reference comparison; long zero-input decay; rate/block partition coverage. Concrete bounds NS-1…NS-11 are defined in ADR-003 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phase1-s2-verification-plan.md | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S2" above** |
| Parameter transitions | Endpoints, invalid values, repeated retargeting and changing block partitions; no discontinuity from smoother state reset; signal-transition metrics plus audition; no allocation or blocking on render path. Concrete cases PT-1…PT-9 are defined in ADR-004 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phase1-s2-verification-plan.md. These cases run against the same ADR-005 fixture; `T60_min`, `T60_max` and `D_max` remain deferred | Sol approves semantics; Terra implements; Luna verifies |
| Modulation experiment | Fixed baseline retained; endpoint/rate stress; interpolation boundary tests; measured decay and output growth under worst-case combinations; explicit approval before enabling | Sol reviews stability limits; Terra experiments; Luna reproduces |
| Sonic acceptance | Impulse and repeatable musical corpus; recorded peak/RMS/decay/stereo measurements; listening notes identifying ringing, onset density, width and unintended pitch movement | Terra prepares; owner auditions; Sol reviews consequences |

For all future tests: use named cases, print the failed condition, and return nonzero on failure even in Release. Preserve seeds, parameter settings, sample rates and block sequences with evidence. Missing measurements are **unmeasured**, never passes. Report output overshoots and safety interventions rather than hiding them by clipping or regenerating reference data. Exact byte comparisons apply only where the operation/toolchain contract supports them; nonlinear or transcendental implementations need justified numerical tolerances.
