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

Luna independently verified the Phase 1 architecture-decision deliverables — ADR-002 (docs/decisions.md), the S1 task plan (docs/phases/phase1-s1-plan.md), and the two cross-reference edits to roadmap.md and dsp-design.md — against the actual files on disk, not against Sol's or Terra's self-reports.

Checked and passed:

- **Scope compliance.** Only the four documents above changed; no file under `src/`, `tests/`, or `tools/`, and no `CMakeLists.txt`, was created or modified. Nothing was committed by Sol or Terra. This is Phase 1's binding exit criterion: an architecture decision and a task plan, no implementation.
- **Cross-reference consistency.** roadmap.md's Phase 1 row 1 and dsp-design.md's "PLANNED: Phase 1 architecture" status line were edited narrowly and now agree with ADR-002.
- **ADR-002 scope guard.** Its explicit exclusions cover pitch/spectral processing, Freeze/Bloom/Texture, stereo strategy, UI, parameter semantics and the AUv3 framework choice. It states, without hedging, that the fixed-network stability proof is limited to a time-invariant/unmodulated system and that no modulation is authorized by this ADR.
- **S1 plan coverage.** Every required-evidence item in the "Delay/lifecycle skeleton" gate below is covered by a named test case in docs/phases/phase1-s1-plan.md, and the plan explicitly does not authorize writing any `.h`/`.cpp`/`CMakeLists.txt` file.
- **Arithmetic spot-check.** The Householder diagonal at N=8 (1 − 2/8 = 0.75) and the Hadamard addition count (8·log₂8 = 24) in ADR-002 are correctly computed.
- **Citation spot-check.** One cited URL (Stanford CCRMA, "Achieving Desired Reverberation Times") was fetched and confirmed to resolve to the claimed content.

No sonic, stability, or performance claim is verified by this pass — none exists yet. This review confirms only that the architecture decision is internally consistent, honestly scoped, and free of unauthorized implementation; it is a documentation and scope audit, not a build/test/render verification like Phase 0's.

## IMPLEMENTED: Phase 1 S1 — delay/lifecycle skeleton (2026-09-16)

Terra implemented `DelayLine` (`src/dsp/DelayLine.h`/`.cpp`) exactly per docs/phases/phase1-s1-plan.md's interface, plus `tests/DelayLineTests.cpp` covering all 9 named cases from the plan's table, and wired a new `aetherfield_dsp_delay_tests` CTest target mirroring the existing pattern (separate executable, same `-Wall -Wextra -Wpedantic -Werror`, own `add_test`).

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

**Independent verification (Luna, 2026-09-16).** Luna re-read docs/phases/phase1-s1-plan.md and diffed the actual `DelayLine.h`/`.cpp` against its exact interface block; confirmed all 9 named test cases exist and test what the plan's table specifies (including the `operator new`/`operator delete` allocation-instrumentation case); confirmed none of the plan's explicit non-goals were violated (no feedback matrix, no multiple lines, no damping filter, no runtime-adjustable length/pre-delay/modulation/interpolation, no denormal/NaN/Inf mitigation code); and independently reran the full build and test suite from a separate clean build directory, twice, confirming identical passing results before deleting that directory. Luna also confirmed `CMakeLists.txt`'s new test target matches the plan's "Build wiring" section (separate executable, not merged into `aetherfield_dsp_tests`; same warning flags; own CTest registration). All checks passed; no spec deviation was found.

This closes the "Delay/lifecycle skeleton" gate below. No feedback, decay, damping, matrix, stereo, parameter, or sonic-quality claim follows from it — `DelayLine` alone is not a reverb.

## IMPLEMENTED: Phase 1 S2 — fixed late network (2026-09-16)

Terra implemented `FeedbackDelayNetwork` (`src/dsp/FeedbackDelayNetwork.h`/`.cpp`) per docs/phases/phase1-s2-plan.md's interface, plus two small non-breaking additions to `DelayLine` (`peek()`/`push()`, splitting `process()`'s existing behavior into its two independent halves — `process()` itself and all 9 of its S1 tests are unchanged), `tests/FdnTests.cpp` covering one named case per NS-1 through NS-11, and wired a new `aetherfield_dsp_fdn_tests` CTest target mirroring the existing pattern.

**Two design corrections made during implementation, not present in the original plan (see docs/agent-log.md for full detail):** (1) the plan's original capacity design — reserving `round(f_s·t_max)+64` samples per line while using only `mᵢ` of them — is not expressible through `DelayLine`'s actual shipped interface, which has no separate capacity/active-length concept; each line is instead prepared at exactly its own `mᵢ` (ADR-005 (b)'s own named alternative). (2) the plan did not originally expose per-line coefficient accessors that NS-2/NS-3/NS-5 need to check `aᵢ`/`gᵢ`/`mᵢ` directly rather than only through black-box signal behavior; `lineCount()`, `delaySamples()`, `dampingCoefficient()` and `foldedLineGain()` were added. Both corrections are recorded in docs/phases/phase1-s2-plan.md itself, not hidden.

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

This closes the "Fixed late network" gate below for the NS-1…NS-11 cases. No parameter-transport layer, diffusion, stereo strategy, or sonic-quality claim follows from it — `FeedbackDelayNetwork` alone is not a reverb, and its NS-test injection/output-tap convention is explicitly a test-only convention (docs/phases/phase1-s2-plan.md), never a product decision. ADR-006 now decides the product injection vector and output tap design that will replace it, and its DS-1…DS-12 cases; nothing here implements or measures either.

## IMPLEMENTED: Phase 1 DS-A — fixed Schroeder allpass primitive (2026-09-17)

**Scope:** the standalone allpass section only. This does not close the DS
diffusion/stereo gate or authorize full-chain integration. The primitive uses
the existing `DelayLine`, derives nearest-prime lengths from time-domain
inputs, applies the inherited memory cutoff, and reports exceptional values.

**TDD and verification:** the focused test was first run against the missing
source and failed at CMake generation. After the minimal implementation was
added, a fresh Release build and focused CTest passed. The full fresh Release
build then ran all five suites — gain, delay, FDN, parameter and allpass —
with **5/5 passed**, total test time 1.80 s. The focused tests cover the
analytic impulse response, nearest-prime derivation, `g=0` pure-delay
behavior, reset, failed-prepare rollback and non-finite reporting. They do
not claim cascade, stereo, full-path decay, coherence or aggregate-detector
evidence.

**Independent verification (Luna, 2026-09-17):** from a separate fresh
Release build directory, Luna reconfigured, rebuilt and reran all five suites;
all passed in 1.81 s. Luna independently checked the recurrence ordering,
nearest-prime tie behavior, rollback-on-failed-prepare, fault preservation and
reset semantics.

**Task 2 section evidence (Terra, 2026-09-17):** a fresh Release configure in
`build/ds-a-task2` built successfully. The focused CTest passed in **0.06 s**;
the full suite passed **5/5** in **1.89 s**. The allpass executable directly
measured **0.05 s real time** on this host; this is observational only, not a
CPU budget. A test-local double recurrence, using the section's stored float
coefficient and the same `1e-20` memory cutoff, compared separate impulse and
fixed-seed bounded-noise renders over all eight ADR-006 delay targets at both
fixture rates: maximum absolute error was **1.02533175e-07** (limit `2e-5`).
The test-local radix-2 double FFT reproduced an impulse exactly and a delayed
impulse with maximum phase error **2.762268411e-12**. Measured impulse responses
were drained for `128*d`, zero-padded to at least 131072, and checked over
65537 bins from zero through pi for all 16 configured sections: maximum
magnitude error was **3.974004226e-08** (limit `1e-6`).

Double-accumulated relative energy error was at most **1.624351376e-08** for
an impulse and **3.249906728e-09** for 4096 samples of fixed-seed noise plus
`128*d` zeros (limit `1e-5`). The prescribed reversed-sign, `M=64*d`
single-section input measured peak **2.23606801** against the stored-coefficient
bound **2.23606801** plus `1e-5`; this is not a cascade 25x/5x measurement.
For every configured length, both the derived coefficient and `g=0.9` reached
exact silence from the stated conservative cutoff bound through two additional
delay cycles; `g=0` ended after `d+1`. Finite `FLT_MAX` sustained for more than
two cycles at `g=0.9`, NaN, and infinity each raised the sample fault flag;
reset then restored exact silent, no-fault output. Test-local regular, array,
and aligned allocation overrides recorded a **zero** process/reset allocation
delta after preallocation. Two identical scripts and partitions
`{1,13,64,512,ragged}` produced float-bit-identical output.

This closes only standalone section portions of DS-1/2/4/5/10/11/12. It does
not measure allpass cascades, FDN interaction, stereo taps/coherence, full-path
decay or silence, shared aggregate fault ownership, host-block processing, or
product CPU/perceptual behavior; the full-chain follow-up remains required.

## IMPLEMENTED: Phase 1 DS-B Task 1 — transactional lifecycle/preparation (2026-09-17)

**Scope:** `DiffusionStereoPath` owns and transactionally prepares the four
input, two-left-output and two-right-output allpass sections, existing FDN and
`ParameterAutomation`. Its aggregate-fault count/latch/reset-pending storage is
created with lifecycle semantics: reset clears current latches/recovery state
while preserving the cumulative count. This task intentionally has no audio
`process` API, FDN pre-step tap accessor, input injection, output diffusion,
stereo output, Mix application or per-sample fault observation.

**TDD and verification:** the new focused target was configured first and
failed because `dsp/DiffusionStereoPath.h` was absent. After the minimal
ownership/preparation implementation, a fresh Release configure/build in
`build/ds-b-task1-final` succeeded. The focused CTest passed **1/1** in
**0.00 s**; the full suite passed **6/6** in **2.02 s**. The focused tests
cover reset before prepare, exact ADR-006 time-domain length derivation at
48 kHz (`{47,103,223,479}`, L `{191,307}`, R `{241,383}`) and 44.1 kHz
(`{43,97,199,439}`, L `{173,281}`, R `{223,353}`), clean aggregate initial
state, reset configuration preservation, and rollback after invalid rate,
allpass time, coefficient, FDN line count and automation `dMaxDb` inputs.

The tests do not exercise a generated wrapper fault because Task 3 has not
introduced a processing boundary or fault observation. FDN count saturation,
latch-only observation, next-nonempty-block reset, FDN taps, sample order and
all full-chain DS measurements remain Tasks 2–4 work.

**Task 1 validation correction:** a new focused test was first run against the
previous implementation with
`cmake --build build/ds-b-task1-final --target aetherfield_dsp_diffusion_stereo_tests --parallel && build/ds-b-task1-final/aetherfield_dsp_diffusion_stereo_tests`.
It exited **1** with `FAIL: invalid realized diffusion topology accepted`.
The replacement preparation gate rejects duplicate input times, non-increasing
interleaved output times, any GCD conflict among all eight realized diffusion
lengths or with a realized FDN delay, and
`max(diffusion) >= FDN m_min`; each rejection leaves an established live
configuration unchanged. It also rejects every FDN/allpass time-derived target
outside `[1, INT_MAX - 1024]` before candidate allocation, covering the finite
enormous `fdnMaxDelaySeconds` case that would otherwise reach the FDN's
out-of-range `llround` path.

A fresh Release run used
`cmake -S . -B build/ds-b-task1-validation -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build/ds-b-task1-validation --parallel`,
`ctest --test-dir build/ds-b-task1-validation -R aetherfield_dsp_diffusion_stereo_tests --output-on-failure`,
and `ctest --test-dir build/ds-b-task1-validation --output-on-failure`.
Configuration and build succeeded; the focused suite passed **1/1** in
**0.01 s** and the full suite passed **6/6** in **1.88 s**. The allocation
counter test observed no candidate allocation for the rejected enormous FDN
target. These are preparation-only checks; Task 2's tap/order API and all
processing, recovery and full-chain evidence remain unimplemented.

## IMPLEMENTED: Phase 1 DS-B Task 2a — read-only FDN pre-step taps (2026-09-17)

**Scope:** `FeedbackDelayNetwork::preStepTapSums() const noexcept` returns
unnormalized even/odd sums of the current `DelayLine::peek()` values in the
existing increasing-delay order. `DiffusionStereoPath` forwards that view and
returns `{0,0}` before preparation. The accessor is const, allocation-free and
does not use or mutate FDN scratch storage, coefficients, detector state or
line state. `FeedbackDelayNetwork::process()` and `processSample()` retain
their existing behavior. Task 2a itself added no wrapper process API, input
injection, routing, output diffusion, Mix, stereo output or aggregate-fault
observation.

**TDD and verification:** before implementation,
`cmake --build build/ds-b-task1-validation --target aetherfield_dsp_fdn_tests aetherfield_dsp_diffusion_stereo_tests --parallel`
exited **2** because `FeedbackDelayNetwork` had no `preStepTapSums` member.
The new FDN fixture injects one impulse, advances `m_min - 1` zero samples,
then observes the known pre-advance sums `{even=1, odd=0}` through a const
view; repeated reads are identical and the next returned FDN wet sample equals
their sum. Wrapper tests confirm that its unprepared and prepared-reset views
are silent and unchanged by repeated reads.

A fresh Release run used
`cmake -S . -B build/ds-b-task2-final -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build/ds-b-task2-final --parallel`,
`ctest --test-dir build/ds-b-task2-final -R 'aetherfield_dsp_(fdn|diffusion_stereo)_tests' --output-on-failure`,
and `ctest --test-dir build/ds-b-task2-final --output-on-failure`.
Configuration and build succeeded; focused FDN/wrapper CTest passed **2/2** in
**1.17 s**, and all legacy/current suites passed **6/6** in **1.82 s**.
At that Task 2a handoff, wrapper sample ordering and partition identity could
not yet be measured because no wrapper advance/process API existed.

## IMPLEMENTED: Phase 1 DS-B Task 2b — one-sample diffusion/stereo routing (2026-09-17)

**Scope:** `DiffusionStereoPath::processSample(float) noexcept` advances the
prepared automation and FDN exactly once, sanitizes a non-finite host injection
to zero, routes mono through the four input allpasses, applies `1/sqrt(N)`
injection normalization, reads the even/odd FDN sums before FDN advancement,
applies `1/sqrt(N/2)` tap normalization, runs the two assigned output allpasses
per channel, and applies this sample's dry/wet Mix gains. It is allocation-free
and returns silence before preparation. It does not implement block processing,
cumulative aggregate-fault accounting, latching or recovery; those remain Task
3. No decay, coherence, channel-balance, silence or perceptual claim follows.

**TDD and verification:** the focused target first failed to compile because
the wrapper lacked both `StereoSample` and `processSample()`. The new test
prepares an independent composition of the existing allpass, FDN and automation
primitives and compares 4,096 impulse-response samples exactly against the
wrapper, including the first FDN arrival. It also counts no allocations across
1,024 warmed `processSample()` calls. A first reference mismatch at the FDN
arrival was traced to the test oracle using division instead of the required
stored-factor multiplication; the corrected oracle uses the same specified
`float` evaluation order.

A fresh Release run used
`cmake -S . -B build/ds-b-task2b-final -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build/ds-b-task2b-final --parallel`,
`ctest --test-dir build/ds-b-task2b-final --output-on-failure`, and
`git diff --check`. Configuration/build succeeded, all **6/6** CTest suites
passed in **1.84 s**, and the whitespace check exited zero.

## IMPLEMENTED: Phase 1 DS-B Task 3 — aggregate faults and block recovery (2026-09-17)

**Scope:** `DiffusionStereoPath::process()` establishes the block boundary:
zero frames are inert; a pending reset clears every allpass, the FDN,
automation and current wrapper latches before the next nonempty frame; the
cumulative wrapper count survives. `processSample()` treats itself as a
one-sample block. Every allpass `Sample::nonFinite` is observed before
propagation. The FDN count/latch is snapshotted around processing; an upstream
source suppresses only FDN's matching input-substitution count, while other FDN
count deltas aggregate. A false-to-true FDN latch without a count delta records
one non-exact aggregate observation. The existing FDN processing semantics are
unchanged; its test-only detector-state seam exists solely to characterize that
saturated-counter path.

**TDD and verification:** the focused target initially failed because the
wrapper had no block `process()` API. Fault tests cover a wrapper-head NaN,
zero-frame preservation of pending recovery, recovery before the following
nonempty silent block, repeated recovered blocks, and input-allpass overflow.
The saturation fixture sets the FDN count to `size_t` maximum with its latch
clear, drives the first allpass overflow at frame 48, and observes four input
stage events plus exactly one FDN latch-only aggregate observation. Fixed
parameters are bit-identical across `{1,13,64,512,3}` block partitions; warmed
sample and block calls allocate zero times.

A fresh Release run used
`cmake -S . -B build/ds-b-task3-final -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build/ds-b-task3-final --parallel`,
`ctest --test-dir build/ds-b-task3-final --output-on-failure`, and
`git diff --check`. Configuration/build succeeded, all **6/6** CTest suites
passed in **1.83 s**, and the whitespace check exited zero. Task 4's decay,
coherence, silence and channel measurements remain unimplemented.

**Independent verification (Luna, 2026-09-17).** Luna rebuilt Release from a
separate clean directory (`/tmp/luna-verify-ds-b-task3`, deleted afterward)
and ran CTest twice: **6/6** passed both times (1.80 s, then 1.82 s),
confirming determinism. Luna traced, rather than trusted, five specific
claims against the actual source: (1) `process()`'s zero-frame guard
(`if (count == 0 || !state_) return;`) executes before the `resetPending_`
check, so a zero-frame call cannot consume a pending reset; (2) `reset()`
clears every input/output allpass section, the FDN and automation, while
leaving `nonFiniteCount_` untouched, so the cumulative count survives; (3)
`observeFdnFault()`'s `latchTransition && !countAdvanced` branch is exactly
the "latch-only" path, producing one aggregate observation when the FDN
count is already saturated; (4) the same function's
`upstreamFaultObserved && delta > 0` branch decrements `delta` by one before
counting, which is the de-duplication that stops a wrapper-observed
non-finite injection from being double-counted when it also advances the
FDN's own counter; (5) the FDN's `setNonFiniteStateForTest()` seam and the
wrapper's forwarding call are both compiled only under
`#if defined(AETHERFIELD_TESTING)`, and all 11 original NS tests still pass
unmodified. Luna's own re-inspection turned up a few off-by-a-handful line
numbers relative to the current file (attributable to normal drift during
review, not a content error); the cited conditions and guards were
independently confirmed present and correct against the actual source when
re-checked. Scope was confirmed via `git diff --check` (exit 0) and a diff
summary showing only the Task 2/3 files (`FeedbackDelayNetwork.h/.cpp`,
`FdnTests.cpp`, `DiffusionStereoPath.h/.cpp`,
`DiffusionStereoPathTests.cpp`) — no Task 4 measurement, modulation, stereo
rendering or undisclosed production drift. Overall verdict: **PASS**, no
deviation from this entry's claims found.

## IMPLEMENTED: Phase 1 DS-B closure — Tasks 1–4, DS-1…DS-13, and Sonic acceptance (2026-09-18)

DS-B Tasks 1–4 are complete. The task's DS-1…DS-12 evidence and its later
propagated DS-10 cessation bound are recorded below; DS-13 adds a whole-path
per-channel magnitude response. `DiffusionStereoPath` control forwarding and
the two offline stereo render tools are implemented. Three owner listening
rounds and Sol's review close the DS-B Sonic acceptance component with no
architectural revision warranted. This is closure for the fixed evaluation
baseline only: it does not authorize a product signal path, host/UI work,
modulation, Freeze, Bloom, Texture, or a final product line count.

## Phase 1 DS-B Task 4 — DS-1..12 measured evidence (2026-09-17 record; current, load-bearing evidence)

**Historical framing only:** this record's own "partially implemented" language
describes Task 4's status as of 2026-09-17, before Task 4 closed (see the
IMPLEMENTED section above). The DS-1..12 measurements themselves are current
and still load-bearing — not superseded. Scope: measured evidence for ADR-006's DS-1..12 verification cases against
the Task 1-3 wrapper baseline, per the 2026-09-17 correction note's corrected
contracts (C1-C5). No `src/` file changed; this task is test-only. Split into
two sub-tasks that landed as four commits on `ds-b-task4`, each independently
spec-compliance-reviewed and code-quality-reviewed before the next began:
`e239e5e`/`7469a71` (4a: anti-vacuity infrastructure, DS-1/2/3/5/6/10/11/12)
and `d02b15d`/`0ed0803` (4b: DS-4/7/8/9). `tests/DiffusionStereoAnalysis.h`
(new, 432 lines) holds shared test-only FFT, Welch/MSC coherence, noise,
number-theory and anti-vacuity helpers; `tests/DiffusionStereoPathTests.cpp`
grew from 441 to 2494 lines. Both reviews independently flagged this growth as
large but justified by genuine per-case measurement work, and recommended
splitting the file at the 4a/4b seam (bracket-swept algebra vs. fixed-config
full-wet-path measurement) as a follow-up refactor — **not done here,
recorded as an open item below.**

**Verification commands (fresh Release build, this entry's HEAD):**
```sh
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
git diff --check
```
Build succeeded with zero warnings under `-Wall -Wextra -Wpedantic -Werror`.
All **6/6** CTest suites passed, **3.90 s** total (`aetherfield_dsp_diffusion_stereo_tests`
alone: **2.07 s**, up from Task 3's 0.32 s — expected, since this suite now
performs real spectral/statistical measurement rather than algebra alone).
`git diff --check` exited zero. The code-quality review for 4b additionally
built and ran the suite under `-fsanitize=address,undefined` independently
(clean) and confirmed zero warnings under `-Wall -Wextra -Wshadow`.

### DS-1 — coefficient and pole bound
40 sections checked (14 input + 6 output sections × 2 rates), built directly
from `SchroederAllpass` across the full bracket `K_in∈{2,3,4,5}`,
`K_out∈{1,2}` (never through `DiffusionStereoConfig`, which stays fixed at
`K_in=4,K_out=2`). `g_ap=0.6180339887` held finite and in `[0,0.9]`
throughout. **Largest pole radius `g_ap^(1/d) = 0.9989958869`** (< 1
everywhere).

### DS-2 — allpass magnitude, falsification
16 full cascades (8 input + 8 output, bracket × rate), each with FFT size
≥131072 (≥65537 bins on `[0,π]`). **Max `||A(e^jω)|-1| = 1.733594716e-07`**
(limit `1e-6`).

### DS-3 — delay-length derivation
16 `(K_in,K_out,rate)` combinations: every diffusion delay prime, strictly
increasing, pairwise co-prime with every other diffusion delay and every FDN
line (direct gcd), below FDN `m_min`. The nearest-prime tie-break was
independently re-derived (not compared against ADR-006's hardcoded numbers)
and confirmed to fire at the same four points ADR-006 (c) predicts: input
stage 3 @44.1kHz, input stage 4 @44.1kHz, output stage 1/L @48kHz, output
stage 1/L @44.1kHz.

### DS-4 — energy conservation and full-path decay-law regression
**Energy conservation** (tolerance `1e-5` relative, reusing DS-A's own
single-section limit): worst impulse error **1.227764512e-08**, worst
4096-sample noise error **5.277049398e-09**, across the input K_in=4 chain and
both K_out=2 output chains at both rates.

**Full-path decay** (NS-6's exact fit method — log10|sample| vs. sample-index
linear regression, skipping onset, stopping before the `1e-20` denormal
floor — reproduced independently in this file, NS-6 itself untouched).
**Per ADR-006 correction note C1, no equality or inequality claim is made
against NS-6; both numbers are reported side by side only:**

- **Minimum Decay, high Damp** (T60_0 realized ≈0.0064s both rates), full-scale
  impulse: out_L slope **-4.329246522e-04** (implied T60 0.1443669s) @48kHz,
  out_R **-4.218867928e-04** (T60 0.1481440s); @44.1kHz out_L
  **-4.710116529e-04** (T60 0.1444279s), out_R **-4.553526625e-04** (T60
  0.1493946s). For context only (not a gate): the network's own T60_min slope
  is -9.75e-03 @48kHz, and the slowest input-diffusion pole's own slope is
  -4.363e-04 — the full-path minimum-Decay slope sits within ~1% of that
  diffusion pole and roughly 22x from the network's own T60_min, consistent
  with C1's point that external allpass stages have their own poles.
- **T60_0 = 1.0s, Damp bypassed** (NS-6's own fixture target): full path
  out_L/out_R **-6.259314537e-05 / -6.212046379e-05** @48kHz (implied T60
  0.9985/1.0061s), **-6.755951137e-05 / -6.810432187e-05** @44.1kHz; bare
  network fitted in both NS-6's own window and this task's full-path window
  (**-6.160978934e-05** / **-6.147574602e-05** @48kHz respectively); the
  homogeneous-decay-law ideal is -6.25e-05 @48kHz. All values reported only.

### DS-5 — peak (ℓ1) headroom
Full bracket: largest measured peak **33.36115265** against largest checked
bound **55.90169944** (`√5^5`, the K_in=5 case). The fixed wrapper
configuration's bounds, `√5^4=25.000` (K_in=4) and `√5^2=5.000` (K_out=2),
match ADR-006 exactly. No overshoot beyond bound+1e-5 anywhere.

### DS-6 — echo density (recorded, not gated)
Measured via a zero-crossing-rate proxy on real rendered cascade output
(explicitly labelled "measured zero-crossing rate", not a literal arrival
count, after code review flagged the original "measured" label as
ambiguous), at 1/10/27(`t_min`)/30ms, K_in∈{2,3,4,5}, both rates, against the
idealized amplitude-blind lattice-count formula. The two diverge sharply at
1ms (expected — the lattice model is asymptotic) and converge to the same
order of magnitude by 27-30ms. No pass/fail gate, per ADR-006.

### DS-7 — interchannel coherence (recorded, no coherence-value gate)
A from-scratch Welch magnitude-squared-coherence (MSC) estimator was built in
`tests/DiffusionStereoAnalysis.h` (periodic Hann window, 50% overlap, DC/Nyquist
excluded, silent-bin floor) and independently validated against three
known-answer cases before use: identical channels → MSC=1 exactly; a
17-sample pure delay → MSC median 0.99978 (theory: 1, confirming the estimator
measures coherence, not time-domain correlation); independent noise → mean
0.01717 vs. theoretical bias `1/63=0.01587`. Both spec and code-quality review
independently re-derived this estimator's math (window formula, PSD
normalization, MSC formula, segment/overlap bookkeeping including the
untested partial-trailing-segment branch, later covered by a dedicated
boundary test in commit `0ed0803`) and found it correct.

Measured at 3 T60_0 values (0.25/1.0/3.0s) × 2 rates × 2 excitations = 12
fixtures, each recording segment count/length, window, overlap, FFT length
and silent-bin floor as C2 requires. **MSC is segment-length-limited for a
seconds-long impulse response measured with an 85ms (4096-sample) segment**
— e.g. 48kHz/T60_0=1s/noise: median 0.19129 @4096 samples (63 segments) vs.
**0.60853 @16384 samples** (15 segments); a diagnostic extending one fixture
to 65536 samples (7 segments) reached median **0.95235**, demonstrating
convergence toward ADR-006 C2's ideal (MSC=1 for a fixed linear mono-input
path) as segment length grows relative to T60_0, not a genuine incoherence
finding. Zero-lag normalized cross-correlation and a declared ±5ms short-lag
range are reported separately at every fixture (e.g. 48kHz/T60_0=0.25s/noise:
ρ(0)=0.010545, max|ρ| over ±5ms = 0.058743 at lag 2 samples).

### DS-8 — mono compatibility and the Mix consequence (48kHz, T60_0=1s, recorded/conditional only)
Mix swept at {0, 0.25, 0.5, 0.75, 1.0} via a manual reference harness
(`OrderedReferencePath`, extended with `processSampleDetailed()` to expose
pre-Mix wet channels and Mix gains — verified by both reviews to preserve
bit-exactness against production `DiffusionStereoPath::processSample()`; no
`DiffusionStereoPath` accessor was added). At each point: `E[L^2]`, `E[R^2]`,
`E[L*R]`, `E[(L+R)^2]` all recorded (e.g. Mix=0.5: 0.12541075 / 0.12181167 /
0.09494510 / 0.43711262); the `(L+R)` level relative to out_L falls from
+6.0206dB (Mix=0, dry-only, perfectly coherent) to +2.888216dB (Mix=1,
wet-only) — a 3.13dB swing, consistent with ADR-006 (g)'s predicted 3.01dB
dry-bias mechanism. Mix-independent dry/wet correlation: ρ_L=-0.0034005,
ρ_R=+0.0035824. The conditional `+3.01dB` incoherent-sum figure, checked on
pre-Mix wet channels only: measured **+3.191836dB** against the +3.01dB
prediction — reported as conditional per correction note C5, not gated.
Measured at 48kHz only (ADR-006's DS-8 definition names no rate requirement,
unlike DS-7); extending to 44.1kHz is a small, undone follow-up.

### DS-9 — channel balance, arrival timing, energy centroids
**L/R RMS** (declared 1.0dB tolerance, both rates, noise+impulse): measured
imbalance **0.6300719dB @48kHz / 0.5860281dB @44.1kHz** (noise) — within the
declared gate but only ~37% margin, not the near-zero the "each output
allpass has `|A|=1`" reasoning alone implies. **This is a recorded finding,
not a bug**: per-line FDN gains `g_i=γ_0^m_i` differ by delay length at a
fixed T60, and ADR-006 (e)'s even/odd tap split assigns the network's
shortest (higher-energy) lines to the L channel — equal tap-vector *norm*
does not guarantee equal channel *power* once tapped lines carry different
decay gains. Only exercised at T60_0=1s; expected to widen as T60_0 shrinks,
so 1.0dB is not a rate/decay-independent bound.

**First nonzero arrival**: L at exactly `m_0` (1297 samples/27.02ms @48kHz,
1193/27.05ms @44.1kHz), R at exactly `m_1` (1511/31.48ms @48kHz,
1399/31.72ms @44.1kHz) — **R arrives ~4.5ms later than L**, a full-path
consequence of the even/odd interleave (L gets the network's shortest line)
that ADR-006 (f)'s "both channels respond at the same sample" claim does not
cover (that claim is about the output diffusers alone, in isolation, which
correction note C5 already restricts).

**Full-path energy centroid** (window=3×T60_0): R−L difference **254.69
samples (5.31ms) @48kHz, 184.25 samples (4.18ms) @44.1kHz** — roughly 2x and
1.5x the isolated-chain figure below. Recorded and explicitly not attributed
further; window-dependent for a decaying response.

**Isolated output-diffuser-only centroid** (output cascades alone, no FDN,
matched impulse excitation): L/R = 498.000007/624.0000088 samples @48kHz,
454.0000064/576.0000081 @44.1kHz — reproducing each chain's own total delay
(`Σd`) to within 9e-06 samples. **Difference: 126.0000018 @48kHz,
122.0000017 @44.1kHz, exactly matching ADR-006 (f)'s algebraic 126/122-sample
figure** — an independent numerical confirmation, not a restatement, of that
arithmetic. Code review requested (and commit `0ed0803` added) the underlying
derivation: a unit-energy allpass section's energy centroid equals its delay
`d` exactly (independent of `g_ap`), and centroids add across a cascade.

### DS-10 — numerical safety extension
Non-finite substitution confirmed at the head of the input chain (NaN at
sample 0 of a block produces exactly one wrapper fault; samples 1..4095
bit-identical to an all-zero control). `reset();reset()` bit-indistinguishable
from one `reset()`. Whole-path silence-in/silence-out bit-exact (8192 zero
samples in → exactly 0.0F both channels, zero faults). Proof-template `S_j`
(input-chain: derived bound `√5^j/(1-g)`; output-chain: measured tap-peak ×
`√5^j/(1-g)`, explicitly labelled as fixture-measured since ADR-006 supplies
no closed form for FDN peak propagation) and `D_j=(k_j+1)d_j` recorded per
section, both rates. Actual full-path silence measured separately (**321,914
samples @48kHz, 297,234 @44.1kHz**, over a 1.4M-sample render, after an
initial 300k-sample window was caught by self-review as vacuously short) —
**explicitly not compared to the proof-template `D_j` or any historical
additive bound**, per correction note C3; the FDN's own decay, not the
diffusion sections' drain, dominates this figure.

### DS-11 — determinism and block partitions
Repeated identical-script renders bit-identical; ragged `{7,29,3,211,5}`
partition bit-identical to whole-buffer render, extending the existing
`{1,13,64,512,3}` fixed-partition coverage — together covering DS-11's full
required `{1,13,64,512,ragged}` set.

### DS-12 — cost and realtime safety (counts, not cycles, not a CPU budget)
12 bracket configurations (standalone cascades) prepared and ran fault-free
with zero allocation delta. Wall-clock, reported as observation only: full
wrapper (diffusion+FDN) ≈67-69ns/sample vs. bare FDN alone ≈26-27ns/sample
(±2ns run-to-run jitter observed, as expected for wall-clock).

### Known open items
- The Task 4a/4b seam in `tests/DiffusionStereoPathTests.cpp` (2402 lines) and
  `tests/DiffusionStereoAnalysis.h` (426 lines) should be split into separate
  fixture/analysis/measurement files — recommended by the 4b code-quality
  review as a dedicated, no-behavior-change refactor commit; not done in this
  task.
- DS-8 is measured at 48kHz only; a 44.1kHz leg is a small follow-up.
- The L/R power imbalance (DS-9), the R-later-arrival asymmetry (DS-9), and
  the DS-7 segment-length coherence sensitivity are measured findings this
  task deliberately leaves uninterpreted, per the plan's "leave interpretation
  to review" instruction (DS-4) and correction notes C2/C5's conditional
  framing. None is gated as a pass/fail failure.
- This task establishes measured evidence only. It does not establish sonic
  acceptance (see roadmap's Sonic acceptance gate) and does not itself
  authorize any successor topology, tap, or control decision named in
  ADR-006's "Revisit When" section.

### Task 4 correction round 1 — current status and reproducible evidence (2026-09-17)

This correction supersedes conflicting Task 4 completion, coverage, balance-gate
and DS-10-proof claims above. The focused target was rebuilt and run after the
test-only corrections:

```sh
cmake --build build --target aetherfield_dsp_diffusion_stereo_tests --parallel
./build/aetherfield_dsp_diffusion_stereo_tests
```

It exited 0. The complete focused output was captured during the run; the
values below are the durable measurement record. Full CTest and diff checks
are recorded with this correction's completion report rather than inferred
from the earlier historical command block.

- **I1 / DS-8/9:** the 1 dB L/R balance failure was removed. At Mix=1,
  48 kHz noise/impulse records measured +0.63007195/+0.62488842 dB L/R;
  44.1 kHz measured +0.58602810/+0.56900547 dB. These are recorded values,
  not an acceptance threshold. The harness does not attribute their cause;
  equal per-sample T60 contraction does not establish unequal line energy or
  a shorter-line causal explanation.
- **I2 / DS-10:** every row uses stored `float q_j=0.6180340052` widened only
  for analysis, with `epsilon_float=1e-20F`. The wrapper's actual defaults are
  Decay=0.5, Damp=0, Mix=1, giving `T60_0=1.093397417 s` at 48 kHz and
  `1.093721826 s` at 44.1 kHz; `validConfig().t60ZeroSeconds=4` is not the
  realized automation control state. For the positive stored q, the analytic
  input-section peak factor is `q + (1-q^2)/(1-q) = 1+2q = 2.2360680103`;
  input `S_j=(1+2q)^j/(1-q)` is therefore an input-cessation bound derived
  from the same stored q. Output `S_j` is only a measured-window illustration
  derived from the observed tap peak, not a proved downstream cessation bound.
  Consequently no whole-chain propagated cessation-state bound is claimed.

  | Rate | input `(d,S,k,D)` | output L `(d,S,k,D)` | output R `(d,S,k,D)` |
  | --- | --- | --- | --- |
  | 48 kHz | (47,2.618034101,98,4653); (103,5.854102304,100,10403); (223,13.09017089,102,22969); (479,29.27051238,103,49816) | (191,0.1379010778,92,17763); (307,0.3083561887,94,29165) | (241,0.1379010778,92,22413); (383,0.3083561887,94,36385) |
  | 44.1 kHz | (43,2.618034101,98,4257); (97,5.854102304,100,9797); (199,13.09017089,102,20497); (439,29.27051238,103,45656) | (173,0.0947513633,91,15916); (281,0.2118704924,93,26414) | (223,0.0947513633,91,20516); (353,0.2118704924,93,33182) |

  The separate full-path impulse render is finite and is not compared to a
  historical additive timeout. It observed exact silence from sample 321914
  through 1,400,000 at 48 kHz (1,078,086-sample suffix), and from 297234
  through 1,400,000 at 44.1 kHz (1,102,766-sample suffix). The test requires
  at least 20,000 trailing observed zero samples, so ending a render while
  still nonzero is a failure.
- **I3 / DS-6:** the prior amplitude-blind zero-crossing proxy remains a
  secondary diagnostic. A distinct amplitude-aware density now counts real
  input-diffuser response samples above `1e-6` of that response's peak and is
  reported next to the idealized lattice count. It is explicitly input-diffuser
  scope, not full-path echo density. Its checkpoints include the actual FDN
  first-arrival marker: `m_0=1297` at 48 kHz and `m_0=1193` at 44.1 kHz.
  Example K_in=4 values at that marker are 17,949.113/s (threshold
  1.4900367e-7) at 48 kHz and 16,708.466/s (threshold 1.4589804e-7) at
  44.1 kHz; no density result is gated.
- **I4 / coverage:** this entry is partial. The bracket code does not provide
  an independent double recurrence, and energy, partition/determinism and
  allocation coverage is not cross-product coverage over every cascade/rate.
  DS-8 records powers and covariance at every Mix point, but channel RMS,
  first nonzero arrival, full-path centroid and isolated-diffuser centroid are
  currently recorded only at Mix=1. The integration plan checklist is left
  unchecked for those incomplete requirements.
- **I5:** the measured-silence guard now proves an observed trailing interval,
  not merely that some nonzero sample occurred earlier in a finite render.
- **I6 / DS-4:** full-path impulse fits use log10(abs(sample)) regression,
  stride 64, after onset and before the `1e-18` floor. At minimum Decay/high
  Damp, 48 kHz fits are L `[4070,42515)` / R `[4070,43500)` and 44.1 kHz fits
  are L `[3740,38820)` / R `[3740,40038)`; their slopes are respectively
  -4.329246522e-4/-4.218867928e-4 and -4.710116529e-4/-4.553526625e-4
  log10/sample. At T60_0=1 s, Damp=0, the full-path windows are
  `[4070,144000)` at 48 kHz and `[3740,132300)` at 44.1 kHz. No full-path
  result is compared as an equality or inequality acceptance test with NS-6.
  The 4096-sample, 48 kHz/default-control impulse comparison is the only
  direct bit-exact production-wrapper/reference check. Longer or different
  control/rate `OrderedReferencePath` measurements are controlled-reference
  measurements, not separately direct production-wrapper observations.

Remaining Task 4 gaps are deliberate recorded incompleteness, not pass
criteria: independent double recurrence and complete bracket coverage;
per-Mix DS-8/9 RMS/arrival/centroid coverage; and a propagated C3
cessation-state proof for output stages. No sonic acceptance follows.

### Task 4 bracket-completion pass — current status and reproducible evidence (2026-09-17)

This closes the "I4 / coverage" gap recorded in the correction-round-1 entry
above, for everything except the propagated DS-10 whole-chain cessation
bound (kept open; see below). Four new checks were added to
`tests/DiffusionStereoPathTests.cpp`, called from the same "Task 4a" batch in
`main()`, no `src/` change:

```sh
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
git diff --check
```
Build succeeded with zero warnings under `-Wall -Wextra -Wpedantic -Werror`.
All **6/6** CTest suites passed, **4.05 s** total
(`aetherfield_dsp_diffusion_stereo_tests` alone: **2.24 s**). `git diff
--check` exited zero.

- **DS-2, independent double recurrence** (`testDs2IndependentDoubleRecurrenceAcrossBracket`):
  a second, independently implemented double-precision Schroeder-allpass
  difference equation (its own circular buffer, not sharing code with
  `src/dsp/SchroederAllpass.cpp`, `tests/SchroederAllpassTests.cpp`'s own
  copy, or the radix2Fft-based magnitude check above) is run in parallel
  with the real cascade, sample-for-sample, across all 16 bracket cascades
  (impulse and deterministic noise each), at both rates. **Max absolute
  error 1.964458132e-07** against a stated `1e-5` tolerance (DS-A's own
  single-section version uses `2e-5`; this covers cascades up to 5
  sections).
- **DS-1..12, bracket energy conservation** (`testDs1Through12BracketEnergyConservation`):
  extends DS-4's `cascadeEnergyError` helper (previously exercised only at
  the fixed `K_in=4/K_out=2` wrapper configuration) across all 16 bracket
  cascades, impulse and 4096-sample noise. **Worst impulse relative error
  1.624351376e-08, worst noise relative error 7.09443201e-09**, both inside
  the stated `1e-5` tolerance.
- **DS-11, bracket determinism** (`testDs11DeterminismAcrossBracket`): each
  of the 16 bracket cascades is rendered twice from a fresh instance against
  an identical 2048-sample deterministic-noise script; every pair is
  float-bit identical.
- **DS-12, bracket allocation** (`testDs12AllocationAcrossBracket`): each of
  the 16 bracket cascades is warmed up for 128 samples, then run for 4096
  further steady-state samples; the global allocation counter's delta is 0
  for every cascade.
- **DS-9, Mix-swept RMS/arrival/full-path centroid**
  (`testDs9ChannelBalanceAndCentroids`, rewritten): channel RMS, first
  nonzero arrival and full-path energy centroid are now measured at all five
  DS-8 Mix points (`{0, 0.25, 0.5, 0.75, 1}`), at both rates — previously
  only at Mix=1. Example (48kHz, Mix=0.5): noise RMS_L/RMS_R
  0.3541338045/0.3490152926 (0.1264585715 dB); impulse full-path centroid
  L=1652.000541 / R=1540.028172 samples. The isolated output-diffuser
  centroid (unit impulse, no FDN, no input chain, no dry/wet gain) is
  Mix-invariant by construction and stays measured once per rate, not once
  per Mix — its L/R/difference values (498.000007/624.0000088/126.0000018
  samples at 48kHz; 454.0000064/576.0000081/122.0000017 samples at 44.1kHz)
  are unchanged from the historical record and still reproduce ADR-006 (f)'s
  126/122-sample figure within the existing 0.5-sample tolerance.

**DS-10 whole-chain cessation bound: still open, deliberately not attempted
in this pass.** The output-section `S_j` rows in
`testDs10ProofTemplateAndMeasuredSilence` still use a *measured* tap peak
rather than an *analytically propagated* one. Closing this correctly
requires chaining the input cascade's own analytic peak-gain bound through
the FDN's actual injection topology — the Hadamard matrix `A`, each line's
folded gain `gᵢ/sqrt(N)`, and its damping-filter state, all from ADR-002/003
— via a driven (not free) linear-contraction argument, then translating the
resulting state-norm bound back to an individual tap-sum bound. That is a
genuine numerical-safety derivation against ADR-002/003's exact state
definitions, at the same rigor those ADRs themselves required, not a
mechanical extension of the bracket-cascade pattern used above. It was
deliberately left for its own dedicated, separately reviewed pass rather
than attempted here: an incorrect "proof" would be strictly worse than the
current honestly-labeled gap.

### Task 4 DS-10 propagated whole-chain cessation bound — closed (2026-09-17)

This closes the one remaining gap the bracket-completion pass entry above
left open. `tests/DiffusionStereoPathTests.cpp` gained two new free
functions, `fdnLoopGainLogBound` and `fdnPoleRadiusBound`, plus a
`stageDrainCircuits`/`dampingStateDrainSamples` refactor of the previously
triplicated per-section decay-search pattern, and
`testDs10ProofTemplateAndMeasuredSilence`'s body was rewritten to replace the
measured `measuredTapPeak` with an analytically propagated `analyticTapPeak`.

```sh
cmake -S . -B build/ds10 -DCMAKE_BUILD_TYPE=Release
cmake --build build/ds10 --parallel
ctest --test-dir build/ds10 --output-on-failure
git diff --check
```
All **6/6** CTest suites passed (`aetherfield_dsp_diffusion_stereo_tests`
alone: ~3.2 s). `git diff --check` exited zero.

**Derivation.** Writing the FDN's z-domain loop map as `M(z) = A *
diag(gRawᵢ) * diag(Hᵢ(z)) * diag(z⁻ᵐᵢ)`, submultiplicativity plus the exact
identity `‖A*diag(gRawᵢ)‖₂ = rho = maxᵢ gRawᵢ` (A orthogonal, ADR-002 point
2) give `‖M(z)‖₂ <= rho * maxᵢ[(1-aᵢ)/(1-aᵢ/r) * r⁻ᵐⁱ] =: B(r)` at the real
point `z=r`, which is exactly where `|Hᵢ(z)|` is maximized for fixed `|z|=r`
(the same monotonicity lemma ADR-003's Rationale already uses on the unit
circle, extended off it). `B(1)=rho<1`, `B` is continuous and strictly
decreasing on `(a_max,1]`, and `B(r)->infinity` as `r->a_max+`, so a unique
`r*` solves `B(r*)=1` (found by bisection in log space, since `m_max` can
exceed a thousand samples and `B(r)` overflows `double` in linear space well
before `r` reaches `a_max`); `r*` is a safe bound on the FDN's pole radius.
Picking `R` just outside `r*` with `mu:=B(R)<1`, a Neumann-series/
Cauchy-estimate argument gives `‖h[n]‖₂ <= [sqrt(N)/(1-mu)]*Rⁿ` for the
vector impulse response from the FDN's scalar injection to its per-line
write values — a bound that stays in ℓ2 throughout, so it needs no
ℓ1-of-impulse-response argument (which ADR-003's Rationale notes is
otherwise required for a general driven peak claim, and is not available in
closed form). Convolution against the input cascade's own bounded, and
eventually exactly-zero, driving signal gives a bound uniform in time and
exact once the input cascade's own absolute cessation time is reached;
component-domination (`|qᵢ[n]| <= ‖q[n]‖₂`) extracts a per-line peak with no
further loss. The FDN's own write-value and damping-state drains are then
chained additively with the input cascade's (already-existing) analytic
drain and each output cascade's own two-stage drain into one true absolute
`wholeChainDrain` — summed, not maxed, because each stage's own zero-input
clock starts only once the stage before it has itself fully drained.

**Independent review.** A dedicated review agent re-derived the algebra by
hand end to end (the monotonicity-off-the-unit-circle claim, the exact
operator-norm identities, the Neumann-series/Cauchy-estimate step, the
sum-not-max chaining logic, and the `qMax`/`analyticTapPeak` arithmetic
including the `sqrt(lineCount/2)` tap-scale cancellation) and confirmed all
of it. It also caught one real implementation bug: the damping-state drain
search's initial rest value (`w=0`, before a line's buffer had produced any
output) trivially satisfied its own "below cutoff" check on the very first
iteration, so every line silently reported a damping-state drain of exactly
0 regardless of its actual damping coefficient. Fixed with an
"only accept settling after having genuinely exceeded cutoff at least once"
guard (`everExceededCutoff` in `dampingStateDrainSamples`), justified by
proving the bound sequence is unimodal (a convex combination of a
non-increasing driver with its own history rises at most once while
catching up, then falls monotonically forever after). The review also
flagged that the primary fixture (`validConfig`, `t60ZeroSeconds ==
t60PiSeconds == 4.0`) forces `aᵢ == 0` on every line (ADR-003 (a): `beta =
(gammaPi/gamma0)^m = 1` whenever `T60_pi == T60_zero`), so the
damping-dependent parts of the derivation were never numerically exercised
with real damping. A new `testDs10PropagatedFdnBoundWithNonzeroDamping`
closes that: it prepares an independent FDN with `T60_pi` far below
`T60_zero` (driving several lines' `aᵢ` up to ADR-003's `a_max=0.999`
clamp) and asserts the bisection still converges inside `(a_max,1)`, `mu`
stays in `(0,1)`, and the damping-state drain search still terminates with
a genuine, non-vacuous, per-line result — including that a strongly-damped
line's drain must exceed its own bare delay length, or the damping
recursion would not be doing anything.

**Recorded numbers** (48 kHz / 44.1 kHz): FDN pole-radius bound
`r*=0.9999880013` / `0.9999869176`; chosen decay rate `R=0.9999881213` /
`0.9999870485`; `mu=0.9995334725` / `0.999532932`; `Q_max=4511201485` /
`4132745722`; input-cascade absolute drain `87841` / `80207` samples;
FDN write-value drain `5839906` / `5349058` samples; FDN damping-state
drain `5839906` / `5349058` samples (matching the write-value drain exactly
here because `aᵢ=0` makes the damping filter a pass-through); chained
output-cascade drain `92494` / `85025` samples; **propagated whole-chain
cessation bound `5932400` / `5434083` samples**. The separately measured
actual full-path exact silence (unchanged from the earlier record: sample
`321914` / `297234` onward) sits roughly 18x inside this analytic bound,
consistent with a deliberately conservative (never violated, not
necessarily tight) worst-case proof rather than a fitted estimate — the
same relationship a numerical simulation of this fixture in Python (double
precision, checked against the identical formulas before this was encoded
in C++) showed across 2,000,000 samples with zero violations. Because the
FDN's own drain now dominates `maxDrain`, the measured-silence render grew
from the historical 1.4M-sample minimum to `renderLength = wholeChainDrain
+ 20000` (≈5.95M / 5.45M samples); this is still fast (a few seconds) since
per-sample cost is O(N).

## IMPLEMENTED: Phase 1 parameter transitions (2026-09-17)

Terra implemented `ParameterAutomation` (`src/dsp/ParameterAutomation.h`/`.cpp`) per docs/phases/phase1-pt-plan.md's interface, plus three small additive methods on `FeedbackDelayNetwork` (`processSample()`, `setLineGain()`, `setDampingCoefficientC()` — S2's own 11 tests and `process(count)` contract are unchanged), `tests/ParameterTransitionTests.cpp` covering one named case per PT-1 through PT-9, and wired a new `aetherfield_dsp_param_tests` CTest target mirroring the existing pattern.

**One design correction made during implementation:** the plan's original `publish()` design used temporary heap-allocated vectors for the per-line coefficient derivation, called from `set*()`. Since `set*()` is the control-thread API, this was not itself a contract violation, but it was unnecessarily wasteful and made one test (`testNoAllocationOrBlocking`) fail against its own stricter, more useful goal. Fixed by replacing the temporaries with fixed-capacity scratch arrays sized to `FeedbackDelayNetwork::kMaxLineCount` (16) — `set*()` is now allocation-free too, a strictly stronger guarantee than the plan required.

**Two real test-design bugs were caught and fixed before this evidence was recorded, not after:** both `testEndpointExactness` (PT-1)'s damping-bypass comparison and `testSignalTransitionMetrics` (PT-7)'s sample-to-sample delta measurement initially used render windows shorter than the network's own shortest delay line (`m_min ≈ 1297` samples ≈ 27ms). Since a freshly-reset `FeedbackDelayNetwork` produces exactly zero output until its shortest line has completed one full circulation, both tests were comparing all-zero arrays and passing vacuously — a real defect Terra found while inspecting the diagnostic output (PT-7's delta printed as exactly `0`, which prompted investigation) rather than trusting a green checkmark. Both are now fixed with adequately long windows (several multiples of `m_max`) and an explicit anti-vacuity assertion (`testEndpointExactness` now requires the reference signal to actually be non-zero somewhere in the comparison window). This is recorded here specifically because a test that passes for the wrong reason is worse than a visible failure.

Terra's own build/test evidence, from a clean build directory:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure   # repeated, to check determinism
```

Results: fresh configure/build succeeded with all warning flags on; `4/4` tests passed (`aetherfield_dsp_tests`, `aetherfield_dsp_delay_tests`, `aetherfield_dsp_fdn_tests`, `aetherfield_dsp_param_tests`); the repeated `ctest` run produced identical pass results. Recorded diagnostics: PT-4's every-sample retargeting stress (`T60_0 = T60_max`, minimum damping, full-scale sine, capped at 2,000,000 samples — `10·T60_max` at `T60_max ≈ 186.6s` would be impractically long for a unit test, stated rather than silently shortened) reached a peak magnitude of `7.337` with `nonFiniteCount() == 0`; PT-7's maximum sample-to-sample delta during a full Damp sweep, after fixing the vacuous-window bug, was `0.354`; PT-8 confirmed zero allocation across both a no-change and a change-every-sample render (`~2.3ms` vs. `~8.3ms` wall time for 48,000 samples — the difference is `set*()`'s double-precision derivation cost, not allocation).

**Independent verification (Luna, 2026-09-17).** Luna independently rebuilt from a separate clean build directory (deleted afterward), confirmed identical pass results across two `ctest` runs, and confirmed the recorded diagnostics by rerunning the binary directly. Luna bit-verified the endpoint-exactness requirement specifically: confirmed `publish()` special-cases `decay`/`mix` at `{0,1}` to assigned exact values rather than evaluating the general formula, and independently confirmed `cos(π/2) ≈ 6.12e-17` (not exactly `0.0`) as the concrete reason this special-casing is required, not defensive paranoia. Luna independently recomputed that both original test-design bugs were real (`m_min = 1297` at 48kHz exceeds both the original 200-sample and 960-sample windows) and confirmed both fixes (longer windows, explicit anti-vacuity assertions) are actually in place. Luna confirmed the single-ramped-`cᵢ` design has no code path anywhere that independently ramps `aᵢ`, confirmed `prepare()`'s `publish() → checkForNewTargets() → reset()` ordering is correct (traced in the actual source), and confirmed all of `FeedbackDelayNetwork`'s original 11 NS-1..NS-11 tests still pass unmodified. All 9 PT cases confirmed to test what their verification-plan case requires, with no remaining vacuous-pass risk found. Scope compliance confirmed via `git diff --stat`/`git status --short`. All checks passed; no deviation found.

This closes the "Parameter transitions" gate below for the PT-1…PT-9 cases. No modulation, diffusion, stereo, or sonic-quality claim follows from it.

## HISTORICAL: Sonic acceptance — first impulse render (2026-09-17)

Terra prepared one deterministic impulse-response render exercising the
now-implemented `FeedbackDelayNetwork` + `ParameterAutomation` pipeline:
`tools/render_reverb/main.cpp` (`aetherfield_render_reverb`), a new
offline tool alongside Phase 0's unmodified `aetherfield_render`. Stated,
non-tuned fixture: `N=8`, ADR-005's delay-length derivation, Decay=0.6
normalized (`T60₀ = 3.056s`), Damp=0.3 normalized (`D_max_fixture=48dB`,
test-only), Mix=1.0 (full wet, no dry path), a single full-scale impulse
at sample 0 after the 20ms coefficient ramp settles, rendered for
`min(max(3·T60₀, 2s), 10s)` and peak-normalized to ≈−1dBFS for listening
(original peak/RMS and the applied gain are printed, never hidden).

**This is this gate's "Impulse" component only.** Per its own acceptance
bar ("Impulse **and** repeatable musical corpus; ... listening notes...";
"Terra prepares; **owner auditions**; Sol reviews consequences"), it is
not satisfied yet: the musical corpus does not exist, and the actual
listening notes can only come from the owner. This entry is the "Terra
prepares" step alone.

Terra's own structural verification (not a substitute for listening):
rendering twice and comparing produced byte-identical files (deterministic);
the first ~27ms is exact silence, matching `m_min ≈ 1297` samples — the
shortest line has not yet completed one circulation, exactly as ADR-002's
"a sparse, audibly discrete onset is a structural certainty of this
fixture, by construction" predicts; the RMS envelope, measured in 1-second
windows, decays at approximately **20 dB/s** (867 → 84 → 7.5 → 0.7,
pre-normalization, in Nyquist-agnostic integer PCM units), matching the
`T60₀ = 3.056s` setting's theoretical `−60dB/3.056s ≈ −19.6dB/s` rate to
within measurement granularity. This is end-to-end confirmation that the
decay law holds in actually-rendered audio, not only in the unit test
suites — but it is a numeric/structural check, not a listening judgment,
and per ADR-002 "any listening note taken at S2 is an observation, not an
acceptance," which applies unchanged here.

The rendered file (`artifacts/s2-pt-impulse.wav`, git-ignored per
README's build-artifact policy) is available locally for the owner to
audition. **No sonic-quality claim is made by this entry.** Sol's
"reviews consequences" step and the owner's listening notes remain
outstanding; this gate is not marked satisfied.

### Owner listening notes, round 1 (2026-09-17)

`tools/render_reverb/main.cpp` was extended to accept `decay`/`damp`/`mix`
as optional CLI arguments (defaults unchanged), and five renders were
generated to isolate one control each: `1-baseline` (Decay=0.6, Damp=0.3,
Mix=1.0), `2-damping-off` (Damp=0.0), `3-short-decay` (Decay=0.3,
`T60₀≈0.14s`), `4-long-decay` (Decay=0.75, `T60₀≈14.3s`), `5-blend-mix`
(Mix=0.5). The owner listened to all five. Findings:

- **Decay length (3 vs. 4) was clearly, unambiguously perceptually
  distinct.** This is a genuine positive result: it corroborates, on the
  perceptual side, what the earlier structural check already established
  numerically (§ above: measured RMS decay rate matched the `T60₀`
  setting's theoretical rate). Both the number and the ear agree Decay is
  doing what it's supposed to.
- **Mix (1 vs. 5) was *not* perceptually distinguishable — traced to a
  real flaw in this test's design, not a DSP defect.** The two renders'
  printed peaks (`1.253` vs. `0.886`) have ratio `≈1.414 = √2`, exactly
  the expected equal-power scaling (`wet=1.0` vs. `wet=0.707`) — the
  underlying Mix math is confirmed correct. But the dry path only ever
  received a single-sample impulse (≈21μs), inaudible as a discrete
  event, and each file was independently peak-normalized for listening,
  which erased the one difference (wet-tail level) Mix actually produced.
  **Lesson for future renders: use a sustained dry source, and normalize
  a comparison batch to one shared reference peak, not per-file.**
- **Damp (1 vs. 2, Damp=0.3 vs. 0.0) was only subtly distinguishable.**
  Plausibly consistent with the setting itself: `D_max_fixture=48dB ×
  0.3 = 14.4dB` of extra high-frequency attenuation over the `T60`
  window is a real but moderate effect, and comparing separately-opened
  files is a weaker test than instant A/B toggling. Not evidence against
  the damping filter's correctness (already proven by NS-2/NS-3); a
  perceptual-magnitude observation, not a correctness one.

**A corrected Mix comparison and a more extreme Damp=1.0 comparison were
proposed but not generated in this round** — deferred to a future
listening pass rather than done now. This round's notes are real
evidence toward the Sonic acceptance gate, but the gate remains
unsatisfied: no musical corpus exists, Sol has not reviewed consequences,
and one positive (Decay) plus two inconclusive-by-test-design results
(Mix, Damp) do not constitute a rendered product's sonic acceptance.

## HISTORICAL: Sonic acceptance — DS-B diffusion/stereo wet path first listen (2026-09-18)

A new offline tool, `tools/render_diffusion_stereo/main.cpp`
(`aetherfield_render_diffusion_stereo`), renders a full-scale mono impulse
through the complete measured DS-B wet path (`DiffusionStereoPath`: input
diffusion → FDN → even/odd taps → output diffusion → Mix), using the
identical evaluation-fixture config `validConfig()` in
`tests/DiffusionStereoPathTests.cpp` already exercises — not a tuned
product preset. As first written, `DiffusionStereoPath` exposed no
control-thread API (no `setDecay`/`setDamp`/`setMix`), so the tool could
only vary sample rate and render duration at the wrapper's fixed built-in
automation defaults; **see "Control-thread API" below — this is no longer
the case**, and the tool now takes the same Decay/Damp/Mix CLI overrides
`render_reverb` does. Both channels are peak-normalized by a single shared
gain (not independently per channel), so the render's actual stereo
balance — including DS-9's recorded ~0.6dB L/R RMS imbalance and ~4.5ms
R-later-arrival — is preserved rather than masked.

The owner listened to `artifacts/6-diffusion-stereo_wrapper-defaults.wav`
(48kHz, 8s) and reported it sounded good. **This is a real, positive
listening note, and it is the first one covering the diffusion/stereo wet
path DS-B Task 4 measured** — but it is a single overall impression at the
wrapper's one fixed control setting, not the per-control isolation notes
round 1 above produced for S2/PT, not a musical corpus, and not Sol's
"reviews consequences" step. Per this gate's own acceptance bar, it remains
**unsatisfied**: no musical corpus exists, no detailed notes on ringing,
onset density, width or unintended pitch movement were recorded for this
path, and Sol's review is outstanding. Recorded here as real, honest
partial evidence, not inflated into gate closure.

### Control-thread API: `DiffusionStereoPath::setDecay()`/`setDamp()`/`setMix()` (2026-09-18)

Following the owner's listen above, the owner directly authorized (in
conversation, not via a written ADR or task plan — the DS-B integration
plan's own Tasks 1–4 are complete and did not include this) exposing
`DiffusionStereoPath`'s already-owned `ParameterAutomation` to callers, so
isolation listening (as round 1 above did for S2/PT) becomes possible for
the diffusion/stereo wet path too. Implemented test-first: four new tests
in `tests/DiffusionStereoPathTests.cpp` (rejection before preparation,
non-finite rejection after preparation, acceptance of valid normalized
values, and an end-to-end check that `setMix(0.0)` settles to an exact
dry-only bypass on both channels — reusing ADR-004's already-proven
endpoint-exactness contract) were written and confirmed to fail to compile
(`no member named 'setDecay'`, etc.) before the three-line forwarding
implementation was added to `src/dsp/DiffusionStereoPath.{h,cpp}`. Each
setter is a thin forward to the identically-named `ParameterAutomation`
method, returning `false` unchanged before preparation; no new validation,
ramp, or transport logic was written, since ADR-004's contract already
governs the underlying call.

`tools/render_diffusion_stereo/main.cpp` was updated to use the new API:
it now accepts the same `decay`/`damp`/`mix` normalized CLI overrides
`render_reverb` does, and primes a 960-sample (20ms, ADR-004 (c)) silent
ramp-settle before the impulse, matching `render_reverb`'s own priming
step. Manually exercised: default settings reproduce the prior
`T60₀≈1.0934s` render; `decay=0.1` produces the expected much shorter
`T60₀≈0.0179s`; `mix=0` produces an exactly-1.0 pre-normalization peak
(the untouched dry impulse) with `RMS_L == RMS_R` exactly, confirming the
dry-bypass path.

```sh
cmake -S . -B build/tdd-red -DCMAKE_BUILD_TYPE=Release
cmake --build build/tdd-red --parallel
ctest --test-dir build/tdd-red --output-on-failure
git diff --check
```
All **6/6** CTest suites passed (`aetherfield_dsp_diffusion_stereo_tests`
alone: ~3.2s). `git diff --check` exited zero. No musical corpus or
per-control isolation listening pass was generated in this entry — this
closes the *capability* gap the prior "first listen" entry's tool had, not
the Sonic acceptance gate itself, which remains exactly as unsatisfied as
recorded above.

### Owner listening notes, round 2 — DS-B diffusion/stereo path (2026-09-18)

`tools/render_listening_batch/main.cpp` (`aetherfield_render_listening_batch`)
generated the round-2 batch described above: Decay isolation
(short/baseline/long), Damp isolation (off/full), Mix isolation
(dry/half/wet, sustained-tone source, shared batch normalization — the
round-1 fix), a 3-item synthetic musical corpus (pluck chord, sustained
pad, transient bursts) at defaults, and a stereo-vs-mono fold-down
comparison on the pad. The owner listened and reported, per file:

- **`round2-decay-long_decay0.9_*.wav` (as first generated) began to fade
  but cut off abruptly at the 20s render length.** This was **a real bug
  in the render tool, not reverb behavior**, and the owner's report is what
  caught it: `Decay=0.9`'s realized `T60₀` is **66.7s** (measured directly
  — see the fix commit), and the tool's duration cap was a flat 20s
  regardless of the realized T60, so the render stopped at only ~0.3 T60
  periods (~−18dB from peak), nowhere near an actual decay to silence.
  Fixed by raising the cap to 90s (covers `Decay=0.9` to ~1.3 T60 periods,
  ~−81dB) and the file was regenerated; every other group's shared
  peak/gain was unchanged by the fix, confirming each decay's peak is set
  early, not late.
- **`round2-mix-dry_decay0.5_*.wav` sounded like "an extended tone with
  minimal reverb," hard to judge whether that reflected Mix=0 or the tone
  itself.** This is consistent with, and does not contradict, the already
  code-proven exact bypass (`testSetMixToZeroBypassesToDryExactly`: output
  is bit-identical to the dry input at Mix=0). The methodological lesson:
  a single sustained pure tone is a poor signal for judging "is a subtle
  effect present," because there is no transient/onset shape to listen
  for its absence against — the corpus items (with real attack/decay
  shape) are likely to make Mix's effect easier to judge than a flat tone.
- **`round2-mix-half_decay0.5_*.wav`: "the stereo quality is a bit weird,
  it sounds like it starts mono, then swaps from left to right speaker
  unevenly."** This is a real, new listening finding, not explained by
  anything already measured (DS-7/DS-8/DS-9 used noise or impulse
  excitation, never a sustained pure tone). **Unverified hypothesis,
  offered as a candidate explanation only:** ADR-006 (e)'s even/odd tap
  split means the L and R channels tap *different* delay lines with
  *different* lengths, so each channel has its own, different comb/modal
  structure; a single sustained frequency (unlike broadband noise or an
  impulse) probes one specific point in each channel's response, and if
  that point sits asymmetrically relative to each channel's nulls/peaks,
  uneven or beating left/right balance is a plausible structural
  consequence of the tap design under tonal excitation specifically — not
  necessarily a coding defect. **Not measured or confirmed — flagged here
  for Sol's review and as a candidate follow-up measurement** (e.g. a
  per-channel magnitude-response sweep through the full wet path,
  analogous to DS-2 but for the L/R tap groups together rather than one
  allpass section).
- **`round2-mix-wet_decay0.5_*.wav` was "much more balanced"** than the
  half-mix file — recorded as reported; not further explained here.
- **`round2-stereo-vs-mono-monofold_defaults.wav` "sounds like a mono
  chord"; `round2-stereo-vs-mono-stereo_defaults.wav` "sounds like a
  stereo chord."** A real, positive finding: the two are audibly
  different, confirming the output diffusion/decorrelation stage (ADR-006
  (f)) produces a genuinely perceptible stereo width on real chordal
  material, not an accidentally-collapsed or falsely-wide image. No claim
  is made here about whether the *amount* of width is correct — that is a
  subjective mix judgment outside this gate.

- **`round2-decay-short`/`round2-decay-baseline` and
  `round2-damp-off`/`round2-damp-full`: all four "rendered as expected."**
  Recorded as reported. This corroborates round 1's own decay-length
  finding on the full stereo wet path (not just the bare S2/PT network),
  and — unlike round 1's Damp=0.3, which was "only subtly distinguishable"
  — the more extreme Damp=0.0-vs-1.0 comparison this round gave an
  expected, presumably clearer result; the owner did not report it as
  ambiguous the way round 1's Damp comparison was.

- **`round2-corpus-pluck-chord_defaults.wav`: "at the tail end of the 5
  seconds it almost sounds like a quick shift from stereo chord to mono or
  fewer notes right at the end."** A second, independent listening
  observation pointing at the same general area as the Mix=0.5 finding
  above — but here there is an already-measured candidate explanation
  rather than a wholly new mystery: DS-7 (above) already measured that
  this wet path's inter-channel coherence is **segment-length-limited**,
  converging *toward* ADR-006 C2's ideal of `MSC=1` (a fixed linear
  mono-input path is expected to be coherent) as the measurement window
  grows relative to the decay. A late tail thinning to a few
  longest-surviving, shared modes becoming perceptually more mono-like is
  *consistent with* that already-recorded numeric trend. **Offered as a
  plausible connection, not a verified cause** — nothing new was measured
  to confirm it specifically explains this render.
- **`round2-corpus-sustained-pad_defaults.wav`: "sounds similar but
  doesn't have the tail-end issues."** Recorded as reported; consistent
  with the pad's held, always-present source material occupying most of
  the file (unlike the pluck's rapid decay into a long, source-free tail),
  though this is also not independently verified.
- **`round2-corpus-transient-bursts_defaults.wav`: "pretty good echo
  density... sounds metallic but that may just be due to the transient."**
  The echo-density impression is a positive, real confirmation on genuinely
  transient/percussive material, complementing DS-6's numeric echo-density
  record. **The "metallic" observation is significant and should not be
  understated**: it is the first listening evidence bearing directly on
  ADR-002's own explicitly named open question — *"whether N = 8 with the
  chosen delays sounds like a convincing ambient field, or metallic, is an
  empirical question"* (ADR-002, "What remains a hypothesis"). The owner's
  own hedge is preserved here, not discarded: they attributed it to
  possibly being a property of the synthesized burst source itself (a
  one-pole-lowpass-filtered noise click), not necessarily the network's
  own coloration, and that ambiguity is real and unresolved — a source
  that itself has no metallic quality would be needed to isolate the
  cause, which this corpus item does not provide.

This round's notes are now complete for every round-2 render: one concrete
tool bug found and fixed, two candidate findings flagged for Sol's review
(the Mix=0.5 stereo-swap observation, and the tail/echo-density
observations bearing on ADR-002's metallic-coloration question), and
several reassuring confirmations (dry bypass, audible stereo width,
decay/damp behaving as expected, real echo density on transient material).
**The Sonic acceptance gate remains formally unsatisfied** — this is
listening evidence toward it, not the gate's acceptance itself, and Sol's
"reviews consequences" step has not yet run — but every render generated
this round has now been auditioned.

**Disclosure Sol's review below caught, recorded here because it changes
how the corpus notes above should be read:** every round-2 musical-corpus
render and the stereo-vs-mono comparison (`tools/render_listening_batch/
main.cpp`'s `renderInto(source, decay=0.5, damp=0.0, mix=1.0)` calls) used
**zero damping and 100% wet, no dry signal present at all** — the
fixture's maximum-coloration corner, not a product-representative
setting. This was not stated plainly enough above (only as "the wrapper's
default settings"), and it materially affects how the "metallic" corpus
observation should be weighed — see Sol's review.

## Sol review — DS-B Sonic acceptance consequences (2026-09-18)

The Sonic acceptance gate's "Sol reviews consequences" step for the DS-B
component, dispatched as a high-effort review (per this project's
Sol/Astra/Terra/Luna convention) after round 2's listening notes above
were complete. Full sources read: both ADR-002 and ADR-006 in full, the
DS-B integration plan (confirmed every checkbox including DS-10 closed),
every DS-1..12 section above plus both listening rounds, roadmap.md, and
`tools/render_listening_batch/main.cpp` itself (to check what each
round-2 render actually rendered, which surfaced the disclosure above).
This is a recommendation only — it authorizes no implementation, no ADR
amendment, and no tool change; no files were touched and no build/test
was run producing it.

**Per named ADR-006 "Revisit When" trigger, against all evidence (not
just the numeric measurements):**

- **DS-6 (echo density): not tripped.** The transient-burst corpus item's
  "pretty good echo density" is the only direct perceptual evidence, and
  it is arguably *stronger* than the numeric leg here, since DS-6's own
  zero-crossing proxy is explicitly amplitude-blind and the lattice model
  it's compared against is asymptotic (diverges at 1ms by design).
- **DS-7 (coherence): not tripped, and the trigger as ADR-006 states it is
  stale.** Read literally, DS-7's 65536-sample diagnostic (MSC=0.95235)
  *would* trip a bare "coherence materially above zero" reading — but
  correction note C2 already established MSC=1 is the *expected* result
  for any fixed linear mono-input path, which makes a high MSC unable to
  falsify the uncorrelated-lines assumption ADR-006 (e) rests on. The
  quantity C2 actually made diagnostic (short-lag time-domain correlation,
  `ρ(0)=0.010545`, `max|ρ|`=0.058743 over ±5ms) stays low, and the
  stereo-vs-mono listening confirms it perceptually. Recommended
  documentation fix (no re-decision): restate ADR-006's DS-7 trigger in
  terms of the short-lag correlation C2 defines, since the MSC formulation
  it currently names was retired by C2 itself.
- **DS-8 (mono fold-down): not tripped, but its listening leg was never
  actually exercised.** The measured 3.13dB dry-bias swing matches
  ADR-006 (g)'s predicted 3.01dB mechanism closely. But per the disclosure
  above, the round-2 mono-fold render was Mix=1.0 wet-only — the dry-bias
  effect is by definition a *mixed-signal* consequence (zero at Mix=0 and
  Mix=1, maximal in between) — so the thing DS-8 is actually about was
  never perceptually probed. Real gap, not a failure; the width finding
  that render did produce remains valid on its own terms.
- **DS-5 (peak headroom under ordinary programme material): not
  tripped, and still formally unmeasured under the condition it names.**
  The recorded 33.36/55.90 peak figure came from a deliberately
  worst-case-aligned bracket fixture, not ordinary material. Round 2's
  corpus renders are this project's first ordinary-material runs, and the
  tool already prints each group's pre-normalization peak/RMS to stderr —
  but those numbers were not captured into this file. Recommended fix:
  record pre-normalization peak/RMS for every listening render as a
  standing convention going forward.

**ADR-002's "convincing ambient field, or metallic" open question: the
transient-burst observation does not move it, in either direction.** Four
confounds stack in that one render, and the owner's own hedge (the source
material) is judged the *least* likely of the four: (1) `damp=0.0` is the
fixture's maximum-coloration corner — no real ambient reverb would be
auditioned this way; (2) `mix=1.0` with no dry reference in the file; (3)
the ~27ms structural pre-gap onto sparse arrival, which ADR-005/ADR-006
both predicted in writing as a "sparse, audibly discrete onset" — not new
evidence against `N=8`; (4) the synthesized burst source itself (broadband,
no resonance of its own — least likely culprit of the four). **This must
not be recorded as evidence that N=8 sounds metallic.** Recommended
follow-up, in increasing cost: a dry-reference control render; a Damp
sweep `{0.0, 0.5, 1.0}` on the same item (if "metallic" tracks damp and
mostly disappears by 0.5, it's an evaluation-setting artifact, not
topology); a `mix=0.3` (product-plausible) pass; and, as the only real
path from "impression" to "argued N decision" — a quantified per-band
magnitude-spectrum coloration metric (the standard-deviation-in-dB measure
ADR-002 already cites from Dal Santo/Prawda/Schlecht/Välimäki) computed
across ADR-005's `N∈{4,8,16}` bracket. That last item would need its own
authorization and is not blocking.

**The Mix=0.5 stereo-swap and the pluck-chord tail-thinning are judged the
same underlying phenomenon, not two mysteries** — and neither implicates a
defect. Mechanism: ADR-006 (e)'s disjoint tap support (L taps lines
{0,2,4,6}, R taps {1,3,5,7}) means both channels share the network's poles
but observe *different residues* of them. Broadband/impulsive excitation
averages this out (matching DS-9's measured ~0.6dB RMS symmetry); narrowband
or sparse excitation does not, producing exactly the kind of transient
image wander and late-tail thinning both round-2 notes describe. **This
review does not accept the round-2 entry's tentative DS-7-coherence link
for the tail-thinning finding above — that connection is judged the wrong
mechanism** (DS-7's coherence rises with measurement-*window* length, not
with elapsed time within a decaying signal) **and is superseded by the
modal-residue account here**, itself still a hypothesis, not verified by
new measurement.

**This does not justify ADR-006 (e)'s named successors.** A signed tap
pattern would not change which lines feed which channel, so the observed
per-channel residue asymmetry would survive it untouched — the wrong fix
for the evidence. Dattorro-style intra-line taps (c3) are ADR-006's named
successor for *insufficient echo density* (DS-6), which listening found
adequate — the right tool for a different, untripped trigger. What is
actually missing is a measurement class, not more listening or a tap
change: **no per-frequency information about the whole wet path's `H_L`
and `H_R` exists anywhere in this project's evidence** (DS-2 measured
individual allpass cascades only), and that gap is exactly where both
round-2 findings fell.

**On the already-measured DS-9 asymmetry** (L always arrives ~4.46ms
before R, at every rate/Decay, by construction of the even/odd interleave
putting the shortest line in L): structurally real, not covered by any
named trigger, and so far perceptually unconfirmed (no listener reported
an image pulled left; the one image note describes a wander, not a bias).
Not actionable now; recommended as a named item in a future listening
round since it is cheap to check now and expensive to discover after host
integration.

### Verdict

**The DS-B evaluation baseline (ADR-006, as measured and as auditioned) is
an acceptable continuation basis for further product work. No
architectural revision is warranted by this evidence.** Every named
Revisit-When trigger is untripped; the two new findings both trace to one
already-documented, already-named structural property (disjoint tap
support) that ADR-006 explicitly called a non-decision and never measured
per-frequency; nothing found requires a nonlinearity, a bug, or a topology
change to explain.

**Recommended next authorized task: "DS-B round 3 — per-channel response
and confound isolation"** (measurement plus renders; no DSP change, no ADR
change):
1. A DS-2-style dense sweep of `|H_L(e^{jω})|`/`|H_R(e^{jω})|` through the
   *whole* wet path (pre-Mix), both rates — recorded as a new DS-13 case,
   no pass/fail gate, same as DS-6/DS-7. This is the measurement that
   confirms or kills the modal-residue hypothesis.
2. A cheap listening falsification: regenerate the Mix=0.5 tone render at
   three frequencies (e.g. 220/277/330Hz) — if the "swap" character
   differs per frequency, modal-residue is confirmed perceptually; if
   identical at every frequency, that points toward a defect instead.
3. Metallic-confound isolation on the transient-burst item: dry reference,
   Damp sweep, and a `mix=0.3` pass (the three items above, cost items
   1-3).
4. Record pre-normalization peak/RMS for every listening render (closes
   DS-5's actual trigger condition); add one single-transient centre-image
   check for the DS-9 arrival/level asymmetry.

**Explicitly NOT recommended:** a signed tap pattern; Dattorro-style
intra-line taps; nested allpass sections or a `K_in`/`K_out` change;
reopening `N`, ADR-005's delay set, or `t_min`; changing ADR-006 (g)'s
dry-centring convention; adding pre-delay/early-reflection to close the
27ms gap (a real contributor, but not yet isolated as *the* cause among
four confounds); any modulation of any kind; and further open-ended
listening rounds beyond round 3 (round 3's items have specific,
discriminating answers — if it produces another impression with no
structure behind it, the correct response is a measurement, not a fifth
audition).

**Gate status:** Sol's "reviews consequences" step is recommended complete
for the DS-B component, with this review as its evidence. **The Sonic
acceptance gate itself remains formally unsatisfied**, pending round 3 —
specifically because every round-2 corpus render used a non-product-
representative setting not disclosed clearly enough (see the disclosure
above), the corpus is thin (three synthetic items) against the gate's own
"repeatable musical corpus" wording, and "ringing" and "unintended pitch
movement" were never explicitly assessed (the latter should be a
confirmed null, since no modulation exists — worth one recorded line).
Round 3 as scoped is judged sufficient to close the gate's DS-B component;
nothing beyond it should be treated as blocking.

Per this project's division of labor, this review is Sol's recommendation
only — Astra/the owner has not yet accepted it, and round 3 is not yet
authorized.

## Round 3 — measured (2026-09-18)

The owner authorized round 3 as Sol scoped it above. Measurement and
render generation are complete; the listening portion (below) is
outstanding.

**DS-13 (new case, not one of ADR-006's original DS-1..12): whole-path
(pre-Mix) L/R magnitude response.** Added to
`tests/DiffusionStereoPathTests.cpp`, using the already-existing, already
bit-verified `OrderedReferencePath::processSampleDetailed()`'s
`wetLeft`/`wetRight` fields (no new production accessor). An 8-second
impulse response per rate (at the wrapper's default automation state),
zero-padded and FFT'd; reports `max`/`mean |20·log₁₀(|H_L|/|H_R|)|` across
all retained bins plus the value at 220/277/330Hz (matching the round-3
listening falsification below). Explicitly labelled, in the test's own
output, an **ESTIMATE** truncated far short of DS-10's measured true
finite-time silence — the same window-length caveat DS-7's Welch estimate
already carries, not a new one. No pass/fail gate, same as DS-6/DS-7.

Measured: **48kHz** — max **58.47dB**, mean **6.181dB** across 262,143
retained bins; at 220Hz **+7.679dB**, 277Hz **−2.825dB**, 330Hz
**+4.402dB**. **44.1kHz** — max **59.57dB**, mean **6.145dB** across
262,143 bins; at 220Hz **+7.381dB**, 277Hz **+0.885dB**, 330Hz
**−6.039dB**. The mean figure (~6.15dB, consistent across both rates) is
a real, substantial average difference across the whole spectrum, not an
artifact of one or two near-null bins driving the much larger max figure
up — this is the first quantified evidence toward Sol's modal-residue
hypothesis for the round-2 stereo findings. The per-frequency values
**flip sign between the two rates** (clearest at 330Hz: +4.4dB @48kHz vs.
−6.0dB @44.1kHz) — expected, not an anomaly, since ADR-005 derives every
delay length independently per rate, so a fixed frequency sits at a
genuinely different point in each rate's own comb structure.

**DS-5 evidence gap closed**: every round-2 listening render's own
pre-normalization peak/RMS is now printed by
`tools/render_listening_batch` (previously computed, not captured).
Recorded here for the three corpus items — this project's first
*ordinary programme material* peaks, the condition DS-5's trigger actually
names, as opposed to the deliberately worst-case-aligned bracket fixture
DS-5's existing 33.36/55.90 figure came from: pluck-chord peak
**0.544617** (RMS 0.0575471), sustained-pad peak **0.870556** (RMS
0.200217), transient-bursts peak **0.0654719** (RMS 0.00479341) — all far
under the `√5^4=25` (`K_in=4`) bound; DS-5's trigger condition remains
untripped under this evidence too.

**Round-3 renders generated** via `tools/render_listening_batch` (same
shared-group-normalization convention as round 2):
- `round3-mix-swap-{220,277,330}hz_decay0.5_damp0.0_mix0.5.wav` — the
  Mix=0.5 stereo-swap falsification at DS-13's three measured frequencies.
- `round3-transient-confound-{dry_mix0.0,damp0.0_mix1.0,damp0.5_mix1.0,
  damp1.0_mix1.0,damp0.0_mix0.3}.wav` — metallic-confound isolation on the
  transient-burst item, one shared-normalized group of five.
- `round3-single-transient-center-check_defaults.wav` — one isolated hit
  (not four), for judging the DS-9 arrival/level asymmetry at onset.

Listening notes for all of the above are the one remaining item before
round 3, and the DS-B Sonic acceptance gate, can be considered closed.

### Owner listening notes, round 3 (2026-09-18)

- **`round3-transient-confound-{damp0.0,damp0.5,damp1.0}_mix1.0.wav`, first
  generation (4-hit sequence, 220ms gaps): "all sound the same."** A real
  test-design bug, not a DSP defect — caught, diagnosed and fixed in the
  same turn (see the fix commit above): the 220ms inter-hit gap is far
  shorter than this fixture's ~1.09s `T60₀`, so successive tails pile up
  rather than decaying to a clean, quiet window between hits, masking
  Damp's spectral coloring. Regenerated with a single burst instead of
  four.
- **Regenerated `damp0.0` vs `damp0.5`: "sound the same."** This is
  **consistent with round 1's own already-recorded finding** that
  Damp=0.3-vs-0.0 was "only subtly distinguishable" on the bare S2/PT
  network — the same known nonlinear-at-moderate-settings perceptual
  pattern, not a new concern, and not contradicted by round 2's
  clearly-distinguishable Damp=0.0-vs-1.0 comparison (the extremes, not a
  moderate step). `damp1.0` was not reported as indistinguishable from the
  other two.
- **`round3-transient-confound-damp0.0_mix0.3.wav`: "sounds good."** A
  positive counterpoint to the original "metallic" observation: at a
  realistic wet/dry blend, no complaint was raised.
- **`round3-mix-swap-{220,277,330}hz_decay0.5_damp0.0_mix0.5.wav`: "277Hz
  and 330Hz sound similar to each other but different from 220Hz."** This
  is the falsification round 3 was built to run, and it resolved in favor
  of the frequency-dependence (modal-residue) hypothesis, not a defect:
  the swap character is **not** identical across frequencies. It also
  shows a real, if inexact, correspondence with DS-13's measured numbers
  above: at 48kHz the three frequencies' `|20·log₁₀(|H_L|/|H_R|)|`
  magnitudes are 220Hz≈**7.68dB**, 277Hz≈**2.83dB**, 330Hz≈**4.40dB** — 277
  and 330 are closer to each other in magnitude than either is to 220,
  matching the reported perceptual grouping. Offered as a real
  correspondence, not proof of the specific causal mechanism, which DS-13
  measures the whole-path ratio for but does not, on its own, decompose
  into "which line/residue is responsible."
- **`round3-single-transient-center-check_defaults.wav`: "centered."** A
  positive, reassuring result: despite DS-9's measured, structurally real
  ~4.46ms/~0.6dB L-channel advantage (the network's shortest line always
  landing in L, by construction of the even/odd interleave), a single
  isolated transient's onset image was not reported as pulled to either
  side.
- **Confirmed null, recorded per Sol's review** (worth one line, since no
  fresh listening test is needed to establish it): **no unintended pitch
  movement is possible in this signal path**, by construction — ADR-002
  authorizes no modulation of any kind, and none exists anywhere in
  `DiffusionStereoPath`, `FeedbackDelayNetwork`, `SchroederAllpass`, or
  `ParameterAutomation`'s smoothing (which ramps scalar gain/mix
  coefficients, never a delay length or a resampling operation). "Ringing"
  is addressed by the metallic-coloration confound isolation above rather
  than as a separate listening item.

### Round 3 and the DS-B Sonic acceptance gate: closed (2026-09-18)

Every item Sol's review scoped for round 3 is now complete: the DS-13
measurement, the closed DS-5 evidence gap, and all four listening
falsifications/confound isolations above, including one real test-design
bug (the transient-confound sequence's inter-hit gap) caught by the
owner's own listening report, diagnosed against round 2's already-recorded
contradicting result, fixed, and reverified in the same session — the same
standard of catching a flaw rather than trusting a plausible-looking
result that this project's testing.md has applied throughout. Per Sol's
own stated closing condition ("round 3 as scoped... is sufficient to close
the gate's DS-B component"), **the DS-B component of the Sonic acceptance
gate is now closed**: impulse and a repeatable synthetic musical corpus
exist; peak/RMS/decay/stereo measurements are recorded, including the new
DS-13 per-channel response; and listening notes now cover ringing (via the
confound isolation), onset density (DS-6 plus the transient corpus item),
width (the stereo-vs-mono comparison), and unintended pitch movement
(confirmed null, above). Terra prepared, the owner audited every render
across three rounds, and Sol reviewed consequences — the full division of
labor this gate names.

This does **not** authorize any implementation beyond what has already
been built (the control-thread API and the listening/measurement tooling,
both already committed). Sol's own verdict stands: no architectural
revision is warranted by any of this evidence, and nothing here reopens
`N`, the delay set, the tap design, or any other ADR-006/ADR-002 decision.

### PB-1…PB-8 — parameter bridge mechanism (2026-09-20)

Task 2 of
[phase1-wrapper-skeleton-plan.md](phases/phase1-wrapper-skeleton-plan.md):
a portable, host-independent parameter-event bridge implementing
[ADR-008](decisions/ADR-008-parameter-event-bridge.md) §1–§7 (mailbox
cells, Host-then-UI drain tie-break, render-thread event coalescing, and
the wait-free reset-request flag), built as plain C++ with no Apple
dependency. New files: `src/wrapper/ResetRequest.h`,
`src/wrapper/ParameterBridge.h`, `src/wrapper/ParameterBridge.cpp`,
`tests/ParameterBridgeTests.cpp`; `CMakeLists.txt` gained the
`aetherfield_wrapper` static library and the `aetherfield_wrapper_tests`
executable/CTest entry, added to the existing
`-Wall -Wextra -Wpedantic -Werror` loop. `src/dsp/` was not touched.

Written test-first: `tests/ParameterBridgeTests.cpp` with only the
fixture helpers and PB-1 was confirmed to fail to compile before
`src/wrapper/ParameterBridge.h` existed:

```sh
g++ -std=c++20 -Isrc -c tests/ParameterBridgeTests.cpp -o /dev/null
```
```
tests/ParameterBridgeTests.cpp:1:10: fatal error: 'wrapper/ParameterBridge.h' file not found
    1 | #include "wrapper/ParameterBridge.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

`ParameterBridge`/`ResetRequest` were then implemented, and PB-1 was
confirmed applied through `drain()`. PB-2 through PB-8 (UI-wins tie-break,
generation-gated non-reapplication, `applyHostEvents()` burst coalescing
to the last value per parameter, an empty-batch no-op, drain-before-any-
write no-op, a 1000-cycle allocation check reusing this project's existing
`SchroederAllpassTests.cpp` global-`operator new`/`operator delete`
override pattern, and a `ResetRequest` set/consume/clear round trip) were
added next; each was already handled by the Step 4 implementation, so this
was a green-from-write TDD cycle, verified by actually running the binary
rather than assumed from the design:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Actual observed CTest output (2026-09-20):

```
Test project .../wrapper-skeleton-plan/build
    Start 1: aetherfield_dsp_tests
1/7 Test #1: aetherfield_dsp_tests ....................   Passed    0.41 sec
    Start 2: aetherfield_dsp_delay_tests
2/7 Test #2: aetherfield_dsp_delay_tests ..............   Passed    0.16 sec
    Start 3: aetherfield_dsp_fdn_tests
3/7 Test #3: aetherfield_dsp_fdn_tests ................   Passed    1.31 sec
    Start 4: aetherfield_dsp_param_tests
4/7 Test #4: aetherfield_dsp_param_tests ..............   Passed    0.89 sec
    Start 5: aetherfield_dsp_allpass_tests
5/7 Test #5: aetherfield_dsp_allpass_tests ............   Passed    0.31 sec
    Start 6: aetherfield_dsp_diffusion_stereo_tests
6/7 Test #6: aetherfield_dsp_diffusion_stereo_tests ...   Passed    3.91 sec
    Start 7: aetherfield_wrapper_tests
7/7 Test #7: aetherfield_wrapper_tests ................   Passed    0.18 sec

100% tests passed, 0 tests failed out of 7

Total Test time (real) =   7.17 sec
```

And the actual observed direct-run output of `./build/aetherfield_wrapper_tests`:

```
PB-1 Host write applied through drain(): output diverged from Decay=0.5 default
PB-2 UI write won over same-drain Host write, as ADR-008 section 2 requires
PB-3 a consumed Host write is not reapplied on a subsequent empty drain
PB-4 applyHostEvents() coalesced a 4-event burst to 2 mailbox writes, last value per parameter
PB-5 an empty event batch produces zero mailbox writes
PB-6 draining before any write applies zero parameters
PB-7 allocation delta through 1000 write/drain cycles: 0
PB-8 ResetRequest round trip: set once, consumed once, cleared
ParameterBridge tests passed
```

Scope: this is a mechanism-only, portable-C++ check of ADR-008's mailbox
and reset-flag design under the existing CMake/CTest loop. No AUv3 host,
`AUAudioUnit`, `AURenderEvent`, or Xcode project is involved here — Task 3
of the same plan (below) wires this bridge to real host callbacks, but
still runs under no host/device/`auval`. `ParameterBridge` never calls
anything but `DiffusionStereoPath::setDecay()`/`setDamp()`/`setMix()`, and
`ResetRequest` never touches `DiffusionStereoPath` at all, matching the
design's own stated boundary. (Note: `drain()`'s implementation was
corrected once after this section was first written — it now only counts a
parameter as "applied" when the underlying setter actually accepts the
value, rather than unconditionally — but this changes nothing about the
PB-1…PB-8 output above, since none of these cases exercise the rejection
path; the printed lines are the current, post-fix behavior.)

### AUv3 wrapper skeleton build (2026-09-20)

Task 3 of
[phase1-wrapper-skeleton-plan.md](phases/phase1-wrapper-skeleton-plan.md):
a minimal native `AUAudioUnit` (`src/auv3/AetherfieldAudioUnit.h`/`.mm`,
`AetherfieldAudioUnitFactory.mm`, `Info.plist`) wiring PB-1…PB-8's
`ParameterBridge`/`ResetRequest` into real host callbacks, generated via
`xcodegen` (`platform/apple/project.yml` → `platform/apple/Aetherfield.xcodeproj`,
two targets: `AetherfieldAUExtension` and a minimal container app
`AetherfieldHost`). Implements ADR-010's lifecycle mapping
(`allocateRenderResourcesAndReturnError:` → `DiffusionStereoPath::prepare()`,
rejecting unsupported sample rate/channel count rather than clamping;
`-reset` → ADR-008 §7's wait-free flag, consumed only at the top of the
render callback; `deallocateRenderResources` keeps the C++ instance alive
rather than reconstructing it, preserving the cumulative fault counter) and
ADR-009's already-decided bus/dry-passthrough behavior (stereo-in/stereo-out,
sum-to-mono reduction, bit-exact dry passthrough on `shouldBypassEffect` via
a render-thread-safe atomic snapshot — **not** the still-open `T_silence`
hybrid CPU optimization, which remains unimplemented).

Command actually run (code signing disabled — this machine has no Apple
Developer team configured, and this task requires no device install):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldAUExtension -configuration Debug \
  -destination "generic/platform=iOS" \
  CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO \
  clean build
```

Result: **`** BUILD SUCCEEDED **`**, with 3 remaining warnings, all on the
intentionally-empty `AetherfieldHost` container-app target (a CFBundleVersion
mismatch against its own synthesized/absent parent-app value, and two
missing-UI-configuration warnings for an app with no interface at all) —
none on `AetherfieldAUExtension`'s own code. A fourth warning
(`AetherfieldAudioUnitFactory` not conforming to `NSExtensionRequestHandling`)
was found and fixed during final review by adding a documented no-op
`-beginRequestWithExtensionContext:` stub, matching Apple's own AUv3
templates.

Getting to a real, linking build required six documented, build-forced
corrections to the plan's literal template code — none changing the
decided architecture, all discovered only by actually compiling against
the Apple SDK rather than guessed in advance: `xcodegen`'s `info: path:`
key regenerates (and would otherwise silently clobber) `Info.plist` on
every run, so its `NSExtension`/`AudioComponents` content lives in
`project.yml`'s `info.properties` instead (with a `GENERATED, do not
hand-edit` warning comment added to `Info.plist` itself, empirically
confirmed not to survive a content-changing regeneration); a target-level
`frameworks:` key is silently accepted but non-functional in xcodegen
2.46.0, replaced with `dependencies: - sdk: ...`; `AVAudioFormat`/
`AVAudioFrameCount` need an explicit `#import <AVFoundation/AVFoundation.h>`
(only forward-declared by `AudioToolbox.h`); `AURenderPullInputBlock`'s
real signature takes a caller-owned `AudioBufferList*`, not an
out-parameter, requiring a pre-allocated `AVAudioPCMBuffer` scratch buffer
(sized once in `allocateRenderResourcesAndReturnError:`, matching this
project's existing allocate-only-at-prepare discipline); Apple requires an
embedded extension's bundle identifier to be a child of its container
app's; and the empty container app needs `GENERATE_INFOPLIST_FILE: true`.

Two rounds of code review on this task, before it was accepted, found and
fixed two Critical real-time-audio defects (a `dealloc`/dispatch-timer
teardown race that could use-after-free `_bridge`/`_path`, closed with a
`dispatch_source_cancel` + `dispatch_sync` drain; a `std::vector::resize()`
call inside the render block that could allocate on the audio thread on
the first or a larger-than-seen `frameCount` callback, closed by
pre-sizing a `_monoScratch` ivar in `allocateRenderResourcesAndReturnError:`)
plus five Important issues (unchecked `AUAudioUnitBus` init failure;
missing channel-count validation alongside the existing sample-rate check;
undocumented host-re-fetch assumption on the input scratch buffer's
lifetime across a reconfigure cycle; no generated-file warning on
`Info.plist`; an unverified assumption that `AUAudioUnit` routes every
`shouldBypassEffect` write through this class's setter override) — all
fixed and independently re-verified. A third round found and fixed one
further, narrower defect introduced by the first fix itself: the
`dispatch_sync` teardown could self-deadlock if the AU's own last strong
reference were released from inside the timer handler's own weak-to-strong
resolution; closed with a `dispatch_queue_set_specific`/
`dispatch_get_specific` self-execution guard.

Scope: **compiles and links under Xcode only.** Not run under `auval`, any
host, any simulator, or any device. HT-1 through HT-12
([ADR-012](decisions/ADR-012-host-device-acceptance-catalog.md)) remain
entirely unrun; `src/dsp/` was not touched by this task.

### Hybrid bypass implementation — Task 4 evidence (2026-09-20)

`145a69f` (implementation commit `b2d6d65`) merges Tasks 1–3 of
[phase1-hybrid-bypass-plan.md](phases/phase1-hybrid-bypass-plan.md): the
control-thread controls/bound interface, `TailSilenceBound`, portable
`HybridBypassController` and `aetherfield_hybrid_bypass_tests`, plus AUv3
render integration.

Current-session portable verification (all commands exit 0):

```sh
cmake -S . -B build/hybrid-bypass-reconcile -DCMAKE_BUILD_TYPE=Release
cmake --build build/hybrid-bypass-reconcile --parallel
ctest --test-dir build/hybrid-bypass-reconcile --output-on-failure
scripts/check_dsp_source_drift.sh
```

CTest reported **8/8 tests passed**, including
`aetherfield_hybrid_bypass_tests`; the drift check reported that the portable
and Apple source lists match **7 files**.

The ordinary signed build command below exited **65** because no development
team is configured (local Xcode/CoreSimulator warnings were also emitted):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj -target AetherfieldAUExtension -configuration Release build
```

The following unsigned compilation command exited **0** with
`** BUILD SUCCEEDED **`:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj -target AetherfieldAUExtension -configuration Release CODE_SIGNING_ALLOWED=NO build
```

This is unsigned compilation evidence only. Do not treat it as `auval`, host,
simulator, or device validation: HT-1 through HT-12 and kill-tail UX remain
deferred.

### Signed Release build unblocked (2026-09-20)

Host/device acceptance Task 0 discovery found an already-valid local Apple
Development signing identity (Team ID `W2VVZU52J6`, `joshband@gmail.com`)
in the keychain. `platform/apple/project.yml` was edited to add
`DEVELOPMENT_TEAM: W2VVZU52J6` and `CODE_SIGN_STYLE: Automatic` to both
`AetherfieldAUExtension` and `AetherfieldHost`, `xcodegen generate` was
re-run (xcodegen 2.46.0, matching the pinned version), and the exact
previously-exit-65 command was re-run with provisioning updates allowed:

```sh
xcodegen generate   # inside platform/apple/
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldAUExtension -configuration Release \
  -allowProvisioningUpdates build
```

**Exited 0, `** BUILD SUCCEEDED **`.** Verified as an actual signed
artifact, not just log text, via:

```sh
codesign -dvv platform/apple/build/Release-iphoneos/AetherfieldAUExtension.appex
```

which reported `Authority=Apple Development: joshband@gmail.com (Q8RAXYZQK4)`
and `TeamIdentifier=W2VVZU52J6` on the produced `.appex`. This closes the
prior exit-65 signing blocker for local compilation only. **It is still not
host, simulator, `auval`, or device evidence** — no container app has been
installed anywhere, and HT-1 through HT-12 remain entirely unrun. Whether
this same signing configuration also lets the paired `AetherfieldHost`
container build/install cleanly, and whether a real device/simulator
actually discovers and instantiates the extension, is unverified by this
result and is Task 1's remaining work, not this one's.
`platform/apple/build/` is a local Xcode build product directory, added to
`.gitignore` rather than committed.

The paired container target also builds and signs cleanly with the same
configuration:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldHost -configuration Release \
  -allowProvisioningUpdates build
```

**Exited 0, `** BUILD SUCCEEDED **`.** `codesign -dvv` on the resulting
`AetherfieldHost.app` reports the same `TeamIdentifier=W2VVZU52J6` and
`Apple Development: joshband@gmail.com` authority chain. Two warnings were
emitted and are unresolved, both plausibly relevant to actual device
install/launch though neither is confirmed blocking by this compile-only
result: "All interface orientations must be supported unless the app
requires full screen" and "A launch configuration or launch storyboard or
xib must be provided unless the app requires full screen." `AetherfieldHost`
has no UI content by design (it exists only to embed the extension per
Apple's AUv3 packaging requirement); whether these warnings actually block
installation/App Store validation, or only launching the host app's own UI
(which this project has no plan to ship), is unverified here and belongs to
Task 1's structural-reachability work, not this signing-only result.

There was a third warning present in this same build's log, not called out
above: "The CFBundleVersion of an app extension ('1') must match that of
its containing parent app (null)" — omitted from the original write-up
above by oversight, not because it was judged less relevant; see the
correction note immediately below for why it matters.

### Correction note — "builds and signs cleanly" overstated (2026-09-20)

A same-day simulator build/install attempt (below) found that
`AetherfieldHost.app` **has no Mach-O executable at all** — `xcodebuild
build` exiting 0 and `codesign -dvv` reporting a clean signature (as
recorded above) did not mean a valid, launchable app bundle, because
`codesign` does not require the bundle's declared `CFBundleExecutable` to
actually exist to sign successfully. Direct inspection confirms this for
the *device* build already recorded above, not only the simulator one:

```sh
find platform/apple/build -name AetherfieldHost -type f   # no output — the binary was never produced
codesign -dvv platform/apple/build/Release-iphoneos/AetherfieldHost.app
```

reports `Executable=.../AetherfieldHost.app/Info.plist` — `codesign` fell
back to signing the Info.plist itself as the nearest thing to an
executable, and `Format=bundle` with no Mach-O load commands, because
`AetherfieldHost`'s `project.yml` target has `sources: []`. The missing
CFBundleVersion-match warning above was the first hint of this; it was not
connected to a root cause at the time it was recorded. **"Exited 0" /
"signs cleanly" above should be read as "the compile and codesign build
phases did not error," not "produced an installable app."** This is
exactly the gap `phase1-host-device-acceptance-plan.md` Task 1 already
named in advance: "A container needing code to launch or register the
extension blocks this step pending bounded repair." No source has been
added to fix it yet; see the plan's Task 1 status for the open question of
whether to apply that bounded repair now.

### iOS Simulator build and install attempt (2026-09-20)

Deferring the physical-device leg of Task 1, the same `AetherfieldHost`
target was built for the Simulator SDK (a distinct, real evidence tier the
plan's own Architecture section already anticipates as "simulator checks,"
separately labelled from device evidence, not a substitute for it) against
an existing local simulator (iPhone 17 Pro, iOS 26.4, UDID
`E246B10C-8B46-414B-BE28-715817C66609`):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldHost -configuration Release \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -allowProvisioningUpdates build
```

**Exited 0**, `Signing Identity: "Sign to Run Locally"` (simulator's normal
ad-hoc signing, distinct from the device build's real Apple Development
identity — expected, not a new finding). Booting the simulator and
attempting install surfaced the actual defect:

```sh
xcrun simctl boot E246B10C-8B46-414B-BE28-715817C66609
xcrun simctl install E246B10C-8B46-414B-BE28-715817C66609 \
  platform/apple/build/Release-iphonesimulator/AetherfieldHost.app
```

**Failed** (exit 1): `App installation failed: Unable to Install
"AetherfieldHost" ... AetherfieldHost.app is missing its bundle
executable. Please check your build settings to make sure that a bundle
executable is produced at the path
"AetherfieldHost.app/AetherfieldHost".` This is the same root cause as the
correction note above, now confirmed as an actual OS-level install
rejection rather than only a structural inference from `codesign` output —
the Simulator's installer performs a check the earlier signed-build
verification did not. No physical device is required to reproduce this;
it is not a device-specific defect. `AetherfieldAUExtension` itself was
not independently tested for standalone install (an app extension cannot
be installed without its container), so its own compiled correctness is
unaffected by this finding.

### Bounded repair applied and verified (2026-09-20)

With explicit owner authorization to apply this specific fix, a minimal
source stub was added: `platform/apple/AetherfieldHost/main.m`, a bare
`UIResponder<UIApplicationDelegate>` subclass with no overridden methods,
run via plain `UIApplicationMain` (no scene manifest — legacy
window-based lifecycle; `AetherfieldHost` has no UI and none is added).
`project.yml`'s `AetherfieldHost.sources` changed from `[]` to
`[{path: AetherfieldHost}]`. This is the entire change — no scene
delegate, storyboard, or UI content was added.

After `xcodegen generate`, both builds were repeated clean
(`rm -rf platform/apple/build` before the simulator one):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldHost -configuration Release \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -allowProvisioningUpdates build
```

Exited 0. `find platform/apple/build -name AetherfieldHost -type f` now
finds a real binary (previously none), and `codesign -dvv` now reports
`Executable=.../AetherfieldHost.app/AetherfieldHost` with
`Format=app bundle with Mach-O universal (x86_64 arm64)` (previously
`Format=bundle`, `Executable=.../Info.plist`). Reinstall to the same
simulator:

```sh
xcrun simctl install E246B10C-8B46-414B-BE28-715817C66609 \
  platform/apple/build/Release-iphonesimulator/AetherfieldHost.app
```

**Exited 0 — install succeeded.** `xcrun simctl listapps` confirms
`com.aetherfield.placeholder.AetherfieldHost` is registered on the
simulator. Further, the embedded extension is independently discoverable
at the OS plugin-registration level:

```sh
xcrun simctl spawn E246B10C-8B46-414B-BE28-715817C66609 pluginkit -m
```

reports `com.aetherfield.placeholder.AetherfieldHost.AetherfieldAUExtension
(1.0)`. This is genuine new evidence — the extension is registered with
the system, not merely compiled — but it is **not** the same as HT-1's
"discovers, instantiates and renders": `pluginkit` confirms registration
only; no host (real or harness) has queried
`AVAudioUnitComponentManager`, instantiated the `AUAudioUnit`, or rendered
through it, on simulator or device. That remains unrun.

The device-signed build was also re-run with the same fix and confirmed
consistent: exit 0, a real Mach-O now present
(`Format=app bundle with Mach-O thin (arm64)`, previously `Format=bundle`),
`TeamIdentifier=W2VVZU52J6` unchanged. The CFBundleVersion-mismatch
warning (extension `'1'` vs. container `null`) persists on both builds;
the orientation/launch-storyboard warnings persist on the device build but
were not observed on this simulator build run. None of these three
warnings has been investigated further or confirmed non-blocking; they are
carried forward, not resolved by this fix.

The portable Release configure/build/CTest sequence was re-run from a
clean `build/host-device-verify` directory after these Apple-side-only
changes, to confirm no unintended effect on the DSP core: **8/8 suites
passed** (unchanged from the prior 8/8 baseline).

### AVAudioUnitComponentManager/AUAudioUnit instantiation harness (2026-09-20)

Requested explicitly by the owner as the next step past `pluginkit -m`'s
registration-only confirmation: a minimal XCTest harness that exercises
the actual host-facing discovery/instantiation path a real AU host (AUM,
Cubasis, Logic) uses, rather than only OS plugin registration. This is
evidence-gathering only, not a shipping target — see the header comment
in `platform/apple/AetherfieldHarnessTests/ComponentInstantiationTests.mm`.

**Design:** a new `bundle.unit-test` target, `AetherfieldHarnessTests`,
hosted inside `AetherfieldHost` via `TEST_HOST`/`BUNDLE_LOADER` (so
`xcodebuild test` builds and installs the real container+extension
itself, rather than depending on a prior manual install), with its own
explicit `AVFoundation.framework`/`AudioToolbox.framework` link
dependencies (required separately from the host's, which links neither).
A new `AetherfieldHarness` scheme wires the test target to a `test`
action. The single test method:

1. Builds an `AudioComponentDescription` matching this project's own
   registered identifiers (`aufx`/`Aeth`/`Josh`, from `project.yml`'s
   `AudioComponents` entry — explicit placeholders, not a product
   commitment).
2. Queries `[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
   componentsMatchingDescription:]`, asserting at least one match.
3. Calls `[AUAudioUnit instantiateWithComponentDescription:options:
   completionHandler:]` with `kAudioComponentInstantiation_LoadOutOfProcess`
   (the correct option for an app-extension-based AUv3, not an in-process
   shortcut), waiting on an `XCTestExpectation`.
4. Asserts no instantiation error and a non-nil `AUAudioUnit`.

It does **not** allocate render resources, render audio, exercise
parameters, or touch lifecycle beyond instantiate/implicit dealloc — HT-1,
HT-9, HT-11 and the rest remain separate, unrun categories.

Two build-fix iterations were needed and are recorded for completeness:
`AVAudioUnitComponent.hasCustomView` is unavailable on iOS (macOS-only
API; removed from the log line) and the test target needed its own
explicit framework dependencies (inherited none from `TEST_HOST`).

**Run command:**

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  test
```

**Exited 0, `** TEST SUCCEEDED **`.** The actual runtime log, not just the
pass marker:

```
[AetherfieldHarness] discovered component: name=Reverb manufacturerName=Aetherfield version=1
[AetherfieldHarness] instantiated: class=AUAudioUnit_XH audioUnitName=Reverb manufacturerName=Aetherfield componentName=Aetherfield: Reverb
Test Case '-[AetherfieldComponentInstantiationTests testDiscoverAndInstantiateAetherfieldAudioUnit]' passed (0.348 seconds).
```

`AUAudioUnit_XH` is Apple's real out-of-process XPC proxy class for a
host-instantiated app-extension AU — confirms this went through the
actual extension-hosting mechanism a commercial host uses, not an
in-process stand-in. `componentName=Aetherfield: Reverb` and
`manufacturerName=Aetherfield` match `project.yml`'s registration exactly.

**What this is not:** still Simulator, not physical device; still no
render call (`allocateRenderResourcesAndReturnError:` was never invoked by
this test); still one process, one instantiation, one run — no repetition,
no concurrent instances, no automation, no bypass, no reset. This closes
none of HT-1 through HT-12; it is meaningfully closer evidence than
`pluginkit` alone, not a substitute for any of them.

### Two follow-ons dispatched in parallel (2026-09-20): render extension and warning cleanup

Owner-requested, dispatched as two file-scope-isolated parallel subagents
(not isolated git worktrees — a large amount of the work recorded above
was still uncommitted at dispatch time, and a worktree branches from a
commit, not the dirty working tree, so isolation would have stranded both
tasks on a stale base missing `AetherfieldHost`'s fix and the harness
target entirely). Task (a) was scoped to
`AetherfieldHarnessTests/ComponentInstantiationTests.mm` only; task (b) to
`project.yml`/`xcodegen generate` only; neither touched the other's files,
`src/`, or any `docs/*.md`. Each used its own Simulator UDID and its own
`-derivedDataPath` to avoid build/install contention. Both findings below
were independently re-verified after the fact by rebuilding and
re-running with *both* changes present together (not just trusting each
subagent's own isolated report):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath <isolated path> \
  test
```

Combined result: **0 warnings**, `testDiscoverAndInstantiateAetherfieldAudioUnit`
still **passed**, `testAllocateAndRenderOneBlock` **failed** the same way
independently reproduced — confirming both changes compose cleanly and
neither result was fork-specific noise.

#### (a) Render extension: format negotiation and allocation succeed; the render call itself fails

A second test method, `testAllocateAndRenderOneBlock`, was added
alongside the existing discovery/instantiation test (which is otherwise
byte-for-byte unchanged and still passes independently). It negotiates a
48 kHz/stereo `AVAudioFormat` — this project's only measured rate
(ADR-005/ADR-010) and ADR-009's decided stereo-in/stereo-out bus — sets it
on `inputBusses[0]`/`outputBusses[0]`, sets `maximumFramesToRender = 512`
(drawn from the already-tested fixed partition `{1,13,64,512,3}`, not
invented), calls `allocateRenderResourcesAndReturnError:`, then calls the
AU's `renderBlock` once with a manufactured silent input, then
`deallocateRenderResources`.

Runtime log:

```
[AetherfieldHarness] allocateRenderResourcesAndReturnError: succeeded=1 error=(null)
[AetherfieldHarness] render call returned status=-66745 (noErr=0)
[AetherfieldHarness] output buffer[0]: nonNil=1 byteSize=2048 (expected 2048)
[AetherfieldHarness] output buffer[1]: nonNil=1 byteSize=2048 (expected 2048)
[AetherfieldHarness] output buffers plausibly shaped=1
[AetherfieldHarness] deallocateRenderResources called
```

**Format negotiation and resource allocation both succeeded** (`YES`, no
error). **The render call itself failed**: OSStatus `-66745`, confirmed
against the iOS 27 SDK header (`AudioToolbox/AUComponent.h:855`) as
`kAudioUnitErr_RenderTimeout` — Apple's own doc comment: "The audio unit
did not satisfy the render request in time." The gap between the
allocation-success log and the render-failure log was ~2.4ms, too fast to
be a real multi-second timeout elapsing — this reads as an immediate
rejection rather than an actual wait-and-expire, though the XPC internals
were not investigated further to confirm why; that reading is an
interpretation, not a verified root cause. The output buffers were still
plausibly shaped (non-nil, correct byte size for 512 frames × stereo ×
4 bytes) despite the error status, but no claim is made about actual
sample values — there is no reference signal here to compare against.

**Working theory, not confirmed:** calling `renderBlock` directly from an
XCTest method's own thread is likely not a valid real-time render context
for an out-of-process AUv3 extension. A render-capable harness probably
needs to drive the call through a proper audio engine (e.g.
`AVAudioEngine`) or a real-time-priority context, not a raw direct call
from an arbitrary thread. This was correctly left out of scope for this
pass rather than guessed at further.

**This is a real, reportable limitation, not evidence of a working
render path.** It does not advance HT-1/HT-3 evidence — no repetition, no
partition sweep, no reference-signal check, no physical device, single
instance, and now also: no successful render at all. It does newly show
that allocation/format-negotiation succeed, which the prior
instantiation-only test did not exercise.

#### (c) AVAudioEngine offline-render experiment avoids the timeout, but does not yet prove extension callback execution

With owner approval, a third, separately named XCTest probe was added to
`AetherfieldHarnessTests/ComponentInstantiationTests.mm`. It instantiates the
same component as an out-of-process `AVAudioUnit`, attaches it between an
`AVAudioPlayerNode` and the engine's main mixer, enables
`AVAudioEngineManualRenderingModeOffline` at the existing 48 kHz/stereo/512
frame point, schedules one silent source buffer, and asks the engine to render
one offline buffer. This is intentionally separate from the direct
`AUAudioUnit.renderBlock` test, which remains a reproducible failure rather
than being replaced or weakened.

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath /private/tmp/aetherfield-avengine-probe \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testAVAudioEngineOfflineRenderOneBlock
```

The command exited 0. The selected test passed (1 executed, 0 failures), with
these harness observations:

```
[AetherfieldHarness] AVAudioEngine offline mode enabled=1 error=(null)
[AetherfieldHarness] AVAudioEngine started=1 error=(null)
[AetherfieldHarness] AVAudioEngine offline render status=0 error=(null) renderedFrames=512
```

This is meaningful but deliberately narrow: the engine-managed offline render
request completed where the raw XCTest-thread `renderBlock` request returned
`-66745`. It supports the context-sensitivity theory enough to make an engine
graph the better next harness direction; it does **not** establish the theory
as root cause or a working AU render path. During the same interval, Simulator
logging reported the out-of-process plug-in connection "interrupted while in
use" and then invalidated. The probe has no callback-level instrumentation and
its silent source/output have no sample-value oracle, so it cannot prove that
the extension's `internalRenderBlock` actually processed the frames rather
than that the engine completed graph scheduling around a disconnected node.
The plan's separately authorized controlled-host/diagnostic design remains the
next gate before such a claim. The command also emitted an Xcode DVT
test-host-resolution assertion and Simulator UIKit/CoreAnimation diagnostics;
they are retained as environment/tool diagnostics, not misreported as a clean
warning-free build result.

**Oracle refinement (same day):** the initial one-block probe above was then
replaced with the named test
`testAVAudioEngineOfflineRenderObservesDelayedWetOutput`. It supplies a
one-sample left-channel impulse and makes eight 512-frame offline render
requests (4,096 frames total). The extension's default Mix is wet-only, and
its configured minimum FDN delay is 27 ms (1,296 frames at 48 kHz), so a
non-zero sample after source frame 0 is a concrete, end-to-end observable of a
delayed effect response; simple source passthrough cannot satisfy it.

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath /private/tmp/aetherfield-avengine-oracle \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testAVAudioEngineOfflineRenderObservesDelayedWetOutput
```

The test **failed** its deliberately meaningful oracle. The engine completed
all requested frames, but the measured late peak was exactly zero:

```
[AetherfieldHarness] AVAudioEngine offline mode enabled=1 error=(null)
[AetherfieldHarness] AVAudioEngine started=1 error=(null)
[AetherfieldHarness] AVAudioEngine offline renderedFrames=4096 latePeak=0
... Connection to plugin interrupted while in use.
XCTAssertGreaterThan failed: latePeak 0.000000 is not greater than 0.000000
```

This supersedes any reading of the earlier one-block status-only pass as
render-path progress. The engine can schedule/render its graph bookkeeping,
but this experiment has **no observable delayed AU output**, while the
out-of-process plug-in connection is interrupted. It still does not identify
whether the callback was never entered, entered before an XPC/lifecycle
failure, or produced output subsequently discarded by the graph. It does,
however, rule out treating the AVAudioEngine route as a working replacement
for the raw `renderBlock` call. The failing assertion is intentionally kept as
the reproducible diagnostic, not weakened into a status-only pass.

**Crash-report confirmation (2026-09-21):** the next bounded diagnostic step
found the extension's actual Simulator crash artifact after a fresh reproduction:
`AetherfieldAUExtension-2026-09-21-114143.ips`. It is not a graph-routing
guess: the report records `EXC_BAD_ACCESS` / `SIGSEGV`,
`KERN_INVALID_ADDRESS at 0x0000000000000000`, on the
`AUOOPRenderingServer-*` thread. Its symbolicated top frame is
`__43-[AetherfieldAudioUnit internalRenderBlock]_block_invoke`, source
`AetherfieldAudioUnit.mm:514` — the first dereference of the captured
`inputBufferList` (`inputBufferList->mNumberBuffers`).

This confirms the lifecycle defect: `internalRenderBlock` snapshots
`_inputPCMBuffer.mutableAudioBufferList` and `_monoScratch.data()` into the
returned block, but both are allocated later in
`allocateRenderResourcesAndReturnError:`. A host may fetch/cache the render
block before rendering; if it does so before allocation, the callback captures
null pointers permanently for that block and crashes on its first render. The
host-side "connection interrupted"/zero-output observations are the direct
consequence of that extension crash, not an output-routing or DSP-tail result.
The raw XCTest-thread `renderBlock` timeout is a separate, still-unresolved
host-context issue. No repair was attempted or authorized by this diagnostic.

**Lifecycle repair and red-to-green verification (2026-09-21):** owner-approved
implementation changed only `src/auv3/AetherfieldAudioUnit.mm`. The returned
block now captures AU-lifetime atomic slots, loads the current input-buffer and
mono-scratch pointers at each non-empty callback, and returns
`kAudioUnitErr_Uninitialized` if either resource is absent. Allocation publishes
both slots only after backing storage exists; deallocation clears them before
releasing storage. This preserves the AUv3 lifecycle requirement that rendering
has stopped before teardown while avoiding the former cached-null capture.

The existing engine oracle supplied the red phase: its targeted command failed
with `renderedFrames=4096 latePeak=0` and an interrupted plug-in connection.
After the repair, the same test command exited 0 with
`renderedFrames=4096 latePeak=0.014125`; no interruption was logged. The
separate direct-render test was also rerun and exited 0 with `status=0`, so the
previous `kAudioUnitErr_RenderTimeout` was caused by the extension crash, not
established as an independent XCTest-thread constraint. These are Simulator
harness results only, not HT-1/HT-3 or physical-device acceptance.

**Ragged-block extension (2026-09-21):** the engine impulse oracle now repeats
`{1,13,64,512,3}` rather than issuing fixed 512-frame requests, at both 48 kHz
and 44.1 kHz. Manual rendering and the render buffer remain sized for the
sequence maximum (512); the last request is capped at the 4,096-frame total.
The delayed-wet-output assertion is unchanged. Focused Simulator runs exited 0:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath /private/tmp/aetherfield-avengine-ragged-48k-regression \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testAVAudioEngineOfflineRenderObservesDelayedWetOutputAt48kHz
```

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath /private/tmp/aetherfield-avengine-ragged-44100 \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testAVAudioEngineOfflineRenderObservesDelayedWetOutputAt44100Hz
```

The 48 kHz log reports `renderedFrames=4096 requests=34`
`sequence={1,13,64,512,3}` and `latePeak=0.014125`; the 44.1 kHz log reports
the same frame/request sequence and `latePeak=0.014429`. Each XCTest command
executed 1 test with 0 failures. This is real out-of-process Simulator
rendering across varying callback sizes at both required rates, but is not
HT-1/HT-3 or physical-device acceptance.

**Cached-block reallocation regression (2026-09-21):** the direct harness test
is now `testCachedRenderBlockSurvivesResourceReallocation`. It obtains the
out-of-process AU's `renderBlock` before `allocateRenderResources...`, retains
that same closure through a successful 512-frame render, deallocates and
reallocates resources, then invokes the unchanged closure again. The focused
Simulator command exited 0: both calls returned `status=0`, and reallocation
succeeded. This guards the exact cached-block lifecycle defect and its
reconfiguration variant; it remains one instance/two blocks at 48 kHz stereo,
not HT-1/HT-3 or physical-device coverage.

**Host UIScene lifecycle repair (2026-09-21):** the empty `AetherfieldHost`
container had still launched through UIKit's legacy no-scene lifecycle. A
focused hosted XCTest reproduced UIKit's `UIScene lifecycle will soon be
required` warning. The owner then authorized a deliberately minimal repair:
`platform/apple/project.yml` now asks XcodeGen to generate
`AetherfieldHost/Info.plist` with one application-role scene configuration,
and `AetherfieldHost/main.m` supplies its named
`AetherfieldHostSceneDelegate` with only an empty `UIWindow *window` property.
There is no storyboard, root controller, or other UI behavior.

After `xcodegen generate` (2.46.0), direct inspection of the built host
plist confirmed `UIApplicationSceneManifest` with
`UIApplicationSupportsMultipleScenes = false` and the sole delegate class.
The same focused hosted XCTest was rerun:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS Simulator,id=E246B10C-8B46-414B-BE28-715817C66609' \
  -derivedDataPath /private/tmp/aetherfield-uiscene-green \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testDiscoverAndInstantiateAetherfieldAudioUnit
```

It exited 0. The `.xcresult` reports one passing test, zero failures, and no
runtime warnings on iPhone 17 Pro Simulator (iOS 26.4, arm64). This removes
the observed legacy-lifecycle warning for this focused Simulator launch; it is
not physical-device, commercial-host, or HT-1/HT-3 acceptance evidence.

**First physical-device harness attempt (2026-09-21):** after a physical
destination became available, the same focused test was built, signed, and
run on Josh's iPhone (iPhone 16 Pro Max, arm64, iOS 27.0 build 24A437):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS,id=00008140-001A6D9C21BB001C' \
  -derivedDataPath /private/tmp/aetherfield-uiscene-device \
  test \
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testDiscoverAndInstantiateAetherfieldAudioUnit
```

The build and code signing completed, and the hosted XCTest launched on the
device, but the command exited 65: `AVAudioUnitComponentManager` returned zero
matching components (`matches.count == 0`). The `.xcresult` records 0 passed,
1 failed, and no runtime warnings. This is a failed physical-device discovery
prerequisite, not an HT-1/HT-3 pass and not a diagnosis or authorization for a
follow-on repair. Xcode's post-failure `devicectl diagnose` collection also
failed; its partial diagnostic archive is retained inside the `.xcresult`.

**Physical-device discovery review (2026-09-21):** a temporary harness probe
waited a full 15 seconds for the public registration-change notification while
polling the exact component description. It received repeated notifications
but never found the component, ruling out a simple fresh-install query race;
the probe was removed rather than retained as a timing workaround. A separate
one-variable attempt to make the component display name mechanically match
the manufacturer/description convention also failed and was reverted. The
remaining device-only log clue is `IPCAUClient: bundle display name is nil`:
the extension already had `CFBundleDisplayName`, but the host did not. A
one-variable host-display-name candidate was generated and rerun after the
device reconnected; installation and XCTest launch again succeeded, but the
exact component remained absent and the device logged `IPCAUClient: can't
connect to server (-66748)`. Xcode's iPhoneOS SDK defines `-66748` as
`kAudioComponentErr_NotPermitted`. The candidate was reverted because it did
not repair discovery. This establishes a permission/service-connection failure
on the physical path, not its root cause; no further speculative repair was
retained.

The failed rerun used the same project, scheme, destination, and focused test
as above, with `-derivedDataPath
/private/tmp/aetherfield-device-host-display-name-rerun`; its result bundle is
`Test-AetherfieldHarness-2026.09.21_13-14-43--0400.xcresult` under that path.

**Permission/service root-cause repair (2026-09-21):** the device-only
`kAudioComponentErr_NotPermitted` clue was traced to two missing Audio Unit
security declarations, using the installed iPhoneOS SDK's
`AUAudioUnitImplementation.h`/`AudioComponent.h` and Xcode's own Audio Unit
host template as references. The AU registration omitted `sandboxSafe`, which
means the registered description does not receive
`kAudioComponentFlag_SandboxSafe`; the container app also had no
`inter-app-audio` entitlement, which the host template supplies for an Audio
Unit host. A focused regression assertion was added for the sandbox-safe flag.

The first one-variable repair (`sandboxSafe: true`) produced a fresh signed
device run. The prior `IPCAUClient: can't connect to server (-66748)` line did
not recur, and the component still returned zero matches; the remaining log
was the already-known host `bundle display name is nil`. This is evidence that
the sandbox declaration addressed the NotPermitted boundary, but it is not a
device discovery pass.

The second, permission-specific repair adds
`platform/apple/AetherfieldHost/AetherfieldHost.entitlements` with
`inter-app-audio = true` and wires it through `CODE_SIGN_ENTITLEMENTS`. The
Simulator test build succeeds and the generated AU plist contains
`sandboxSafe = true`. The first physical-device XCTest with this entitlement
does not launch: Xcode rejects the currently managed profile before signing:
`iOS Team Provisioning Profile: com.aetherfield.placeholder.AetherfieldHost`
does not include the Inter-App Audio capability/entitlement. This is the
current external signing blocker, not a test result and not a physical-device
discovery pass. The App ID/profile capability must be refreshed before the
same focused device command can be rerun.

**Inter-App Audio profile refresh and physical-device discovery pass
(2026-09-21):** with the owner's authorization, the same focused command was
rerun using `-allowProvisioningUpdates` after Xcode refreshed the managed
profile. Xcode selected profile `20d27be8-13cb-4997-9da4-ca0015208434`, built
and signed the host/extension/test bundle, installed it on Josh's iPhone 16
Pro Max (iOS 27.0, build 24A437), and launched the XCTest:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \\
  -scheme AetherfieldHarness \\
  -destination 'platform=iOS,id=00008140-001A6D9C21BB001C' \\
  -derivedDataPath /private/tmp/aetherfield-inter-app-audio-device-refresh \\
  -allowProvisioningUpdates test \\
  -only-testing:AetherfieldHarnessTests/AetherfieldComponentInstantiationTests/testDiscoverAndInstantiateAetherfieldAudioUnit
```

Result: **TEST SUCCEEDED**, one test, zero failures. The device discovered
`name=Reverb manufacturerName=Aetherfield version=1` and instantiated
`class=AUAudioUnit_XH` with the expected component and manufacturer names. The
previous `IPCAUClient: can't connect to server (-66748)` / NotPermitted failure
did not recur. The run still logs `IPCAUClient: bundle display name is nil`
from the host; discovery and instantiation passed, but that warning remains a
separate cleanup item. This is physical-device discovery/instantiation
evidence only, not HT-1/HT-3 acceptance: no render, repetition, lifecycle
stress, commercial host, or matrix coverage was run. Result bundle:
`/private/tmp/aetherfield-inter-app-audio-device-refresh/Logs/Test/Test-AetherfieldHarness-2026.09.21_13-30-05--0400.xcresult`.

**Bounded HT-1/HT-3 physical evidence (2026-09-21):** added the separate
`platform/apple/AetherfieldHarnessTests/PhysicalAcceptanceTests.mm` harness.
Its first HT-3 run failed because the harness executed each partition list
only once and consumed only part of the input; that helper defect was fixed
before interpreting any AU behavior. The corrected combined device run
passed 6 tests with zero failures on Josh's iPhone 16 Pro Max (iOS 27.0,
build 24A437). It includes the existing four focused tests, the bounded HT-1
smoke (20 same-instance allocation/render/deallocation cycles and 20 fresh
instantiate/render/destroy cycles at each 44.1 kHz and 48 kHz), and HT-3
float-bit-exact one-shot versus partitioned rendering over 131,072 frames at
both rates for `{1,13,64,512,3}`, `{7,29,3,211,5}`, and
`{0,1,13,64,512,977,1024,3,0}`. Result bundle:
`/tmp/aetherfield-physical-acceptance-final/Logs/Test/Test-AetherfieldHarness-2026-09-21_13-44-41--0400.xcresult`.

This is bounded evidence, not HT-1/HT-3 closure. The full HT-1 100-cycle
contract, controlled-fault persistence/resource-growth checks, HT-3's 4096
and observed-host-maximum coverage, raw-PCM retention, and explicit capacity
rejection probe remain open. The non-blocking host warning
`IPCAUClient: bundle display name is nil` remains separately recorded; the
prior `-66748` NotPermitted discovery failure did not recur. No broader
physical-device acceptance or commercial-host pass is claimed.

**Expanded lifecycle and 4096-frame diagnostic (2026-09-21):** the physical
harness was extended to 100 same-instance one-second allocation/render/
deallocation cycles and 100 fresh instantiate/render/destroy cycles at each
44.1 kHz and 48 kHz. The HT-1 test passed on the connected iPhone 16 Pro Max
(iOS 27.0, build 24A437). The same run added a `{4096}` HT-3 partition while
retaining the prior fixed, ragged and zero-frame sequences. The `{4096}` case
failed bit-exact comparison at the first sample after the first block
(`firstLeft=4096`, `firstRight=4096`) at both rates; the focused rerun recorded
the same result. A subsequent source-level review found that the harness's
`renderBlock:` helper declared `AudioBufferList output = {0}` (storage for one
`AudioBuffer`) and then wrote `mBuffers[1]`, an out-of-bounds stack write at
every callback. The helper now allocates the two-buffer shape explicitly.
Therefore the prior 4096 result is no longer admissible as an AU-boundary
finding until the corrected harness reruns; no production repair was applied.
The existing `{1,13,64,512,3}`, `{7,29,3,211,5}` and
`{0,1,13,64,512,977,1024,3,0}` cases were not changed and remain the prior
bounded evidence set. Result bundle:
`/tmp/aetherfield-physical-acceptance-ht3-debug/Logs/Test/Test-AetherfieldHarness-2026.09.21_13-54-32--0400.xcresult`.
HT-3 remains open pending a corrected-harness rerun and any subsequent
root-cause investigation/authorization. The harness does not issue the separate
over-capacity rejection probe because the current AU callback has no explicit
pre-write capacity rejection and such a call would be unsafe.

**HT-3 harness-boundary correction (2026-09-21):** the two-buffer output-list
storage defect above was corrected in `PhysicalAcceptanceTests.mm`. The
simulator-SDK build of the corrected harness succeeds, but the rerun is
blocked until CoreDevice/CoreSimulator restores a runnable device or
simulator destination. No 4096-frame AU-boundary conclusion is currently
claimed.

**Corrected-harness physical rerun, no reboot (2026-09-21):** a later check
found `xcrun simctl list devices available` and `xcrun devicectl list
devices` both responding again (Josh's iPhone 16 Pro Max listed
`available (paired)`) without the machine having been rebooted; the prior
`simdiskimaged`/CoreSimulator failure had cleared on its own. The corrected
`AetherfieldPhysicalAcceptanceTests` class was run directly on that device:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS,id=00008140-001A6D9C21BB001C' \
  -derivedDataPath /private/tmp/aetherfield-ht3-rerun-no-reboot \
  -allowProvisioningUpdates test \
  -only-testing:AetherfieldHarnessTests/AetherfieldPhysicalAcceptanceTests
```

`testHT1LifecycleSmokeAtBothRates` **passed** (unchanged from the prior
6-test combined run). `testHT3FixedAndRaggedPartitionsAreBitExactAtBothRates`
**failed**, but not on the previously-blocked `{4096}` set: that set, plus
`{1,13,64,512,3}` and `{7,29,3,211,5}`, are now bit-exact at both rates,
confirming the prior `{4096}` failure really was the harness's own
underallocated-buffer defect, not an AU-boundary problem. The failure is
instead on the fourth partition set, `{0,1,13,64,512,977,1024,3,0}` (the one
exercising a zero-frame call), diverging mid-stream rather than at frame 0:
`firstLeft=1193 firstRight=1399` at 44.1 kHz, `firstLeft=1297 firstRight=1511`
at 48 kHz. A second, isolated rerun of only that test method reproduced the
identical mismatch positions at both rates:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHarness \
  -destination 'platform=iOS,id=00008140-001A6D9C21BB001C' \
  -derivedDataPath /private/tmp/aetherfield-ht3-rerun2 \
  -allowProvisioningUpdates test \
  -only-testing:AetherfieldHarnessTests/AetherfieldPhysicalAcceptanceTests/testHT3FixedAndRaggedPartitionsAreBitExactAtBothRates
```

Result bundles: `/private/tmp/aetherfield-ht3-rerun-no-reboot/Logs/Test/Test-AetherfieldHarness-2026.09.21_15-44-49--0400.xcresult`
and `/private/tmp/aetherfield-ht3-rerun2/Logs/Test/Test-AetherfieldHarness-2026.09.21_15-46-*.xcresult`.

This is a genuine, reproducible new physical-device HT-3 finding distinct
from the resolved harness defect: bit-exactness holds for `{4096}` and two
other partition sequences but breaks specifically when a zero-frame render
call is interleaved (partition set `{0,1,13,64,512,977,1024,3,0}`), with the
first divergence roughly 1.1k–1.5k samples in, not at the boundary itself.
No root cause is diagnosed and no repair is authorized here — this needs its
own bounded investigation (candidate area: zero-frame handling in the render
path) before HT-3 can close.

**Portable-core elimination (2026-09-21):** before investigating further on
device, the identical hypothesis was tested directly against
`DiffusionStereoPath` in an isolated scratch C++ program (not part of the
project's own build; excluded from the repository), reusing the same
seed/generator and issuing a `process(..., 0)` call before the real
131,072-frame render, compared byte-for-byte against a render with no leading
zero-frame call, at both rates. Result: **bit-exact identical, no divergence
at all.** Every `count == 0` path was also read directly in source
(`DelayLine::process`, `FeedbackDelayNetwork::process`,
`DiffusionStereoPath::process`) and is a clean early return with no state
mutation. This conclusively rules out the shared portable DSP core as the
source of the divergence; the effect is introduced somewhere at or below the
AU/wrapper boundary. `AetherfieldAudioUnit.mm`'s `internalRenderBlock` was
also read in full: for `frameCount == 0` it returns `noErr` immediately,
before pulling input or touching `path`/`monoScratch`/`hybridBypass` state,
identically on every call. The async 1 kHz `_bridgeControllerTimer` (ADR-008
§1's Bridge Controller) was also considered and ruled out for this harness
specifically: `configureUnit:` never writes Decay/Damp/Mix (the harness runs
entirely on the AU's own defaults), so the timer's `bridge.drain()` never has
a pending host/UI generation to apply and never touches `path` at all during
this test.

**Zero-frame-partition mismatch isolation (2026-09-21):** a new diagnostic
test, `testHT3ZeroFrameIsolationAt44100Hz` (added to
`PhysicalAcceptanceTests.mm`, retained as evidence, not part of the plan's
required matrix), isolates which zero-frame call in the sequence triggers the
divergence, at 44.1 kHz only:

| Variant | Partitions | Result |
|---|---|---|
| `no_zero` | `{1,13,64,512,977,1024,3}` | bit-exact |
| `leading_zero_only` | `{0,1,13,64,512,977,1024,3}` | mismatch, `firstLeft=1193 firstRight=1399` |
| `trailing_zero_only` | `{1,13,64,512,977,1024,3,0}` | mismatch, `firstLeft=2594 firstRight=2594` |
| `both_zeros` | `{0,1,13,64,512,977,1024,3,0}` | mismatch, `firstLeft=1193 firstRight=1399` (identical to leading-only — the earlier divergence dominates before the trailing call is reached) |

Removing all zero-frame calls restores bit-exactness, confirming the zero-
frame call itself is the trigger, not the specific non-zero values. The two
single-zero variants diverge differently in a way that is itself evidence:
`leading_zero_only`'s divergence surfaces ~1,193/1,297 samples later — closely
matching the FDN's own configured 27 ms minimum delay (`fdnMinDelaySeconds`
in `DiffusionStereoConfig`; 0.027 × 44100 ≈ 1191, 0.027 × 48000 = 1296),
consistent with a perturbation introduced before any real audio that only
becomes visible once it has recirculated through the shortest FDN feedback
path. `trailing_zero_only`'s divergence appears immediately at the exact
sample offset where that call occurs (2594 = 1+13+64+512+977+1024+3, the
partition sequence's cumulative sum before the trailing zero), with no such
delay — consistent with a shorter, more immediate part of the signal path.
Both are compatible with the same underlying trigger (a zero-frame render
call disturbing something at or near the AU render boundary) surfacing
through different downstream paths depending on where in the stream it lands,
rather than two unrelated defects. Result bundle:
`/private/tmp/aetherfield-ht3-zero-isolation/Logs/Test/Test-AetherfieldHarness-2026.09.21_15-54-27--0400.xcresult`.

**Zero-frame-partition mismatch: AVAudioEngine rerun (2026-09-21):** since
the direct-call harness uses `AUAudioUnit.renderBlock` from an XCTest thread
(the same mechanism previously implicated in the unrelated `-66745`
RenderTimeout and `EXC_BAD_ACCESS` findings earlier in this project's
history), the leading/trailing-zero comparison was rerun through a real
`AVAudioEngine`-managed offline manual-rendering graph instead, reusing
`ComponentInstantiationTests.mm`'s established engine-setup pattern. New test
`testAVAudioEngineHT3ZeroFramePartitionAt44100Hz` (added to
`PhysicalAcceptanceTests.mm`) renders the same deterministic 131,072-frame
stereo signal through an `AVAudioPlayerNode → AetherfieldAudioUnit →
mainMixerNode` graph in `AVAudioEngineManualRenderingModeOffline`, comparing
a `{4096}`-partitioned reference against the `{0,1,13,64,512,977,1024,3,0}`
candidate (both `maximumFrameCount=4096`).

One authoring defect was hit and fixed before any result was interpretable:
passing the 9-element partition list as an inline brace literal directly
inside an `XCTAssertTrue(...)` call failed to compile (`error: unknown type
name 'maximumFrameCount'`, `error: extraneous closing brace`) because the C
preprocessor only balances parentheses when splitting macro arguments, not
braces — the literal's top-level commas were being read as extra
`XCTAssertTrue` arguments. Fixed by extracting the partitions into named
`const std::vector<AVAudioFrameCount>` locals before the call, matching this
file's own existing working pattern elsewhere. This was a test-authoring
defect in the new diagnostic code, not a finding about the AU under test.

**Result: the mismatch reproduces identically through the engine-managed
path.** `engine renderOffline` accepted every zero-frame request without
error (`status=0`) at each occurrence. The final comparison:
`leftEqual=0 rightEqual=0 firstLeft=1193 firstRight=1399` — the exact same
divergence position as the direct-call harness's `leading_zero_only`/
`both_zeros` result. This rules out "artifact of calling `renderBlock`
directly from an XCTest thread" as an explanation: a real host-managed
`AVAudioEngine` render graph reaches the same result. The effect is a
property of the AU under test when it receives a zero-frame render request,
not of either test harness's calling convention. Result bundle:
`/private/tmp/aetherfield-ht3-engine-rerun/Logs/Test/Test-AetherfieldHarness-2026.09.21_15-59-*.xcresult`.

**Mechanism located, without Instruments (2026-09-21):** rather than reaching
directly for Instruments-level tracing, the test's own input-pull block was
instrumented first — zero risk, no production source touched, reusing code
this session was already editing. New test
`testHT3ZeroFramePullInstrumentationAt44100Hz` (added to
`PhysicalAcceptanceTests.mm`) logs every `pullInputBlock` invocation (or its
absence) and the post-call state of the output buffer, using a `-999.0F`
sentinel pre-fill so "never written" is distinguishable from a legitimately-
silent early wet-tail sample (expected to be near 0.0F before the FDN's
minimum delay has elapsed).

**Result: the render call immediately following every zero-frame request
never invokes the supplied `pullInputBlock` at all**, both after the leading
zero call (`requestedFrames=1 offset=0`: "pull block was NOT invoked") and
after the in-sequence wraparound zero call (`requestedFrames=1 offset=2594`:
same). Despite this, the render call still returns `noErr` (no error is ever
surfaced to the caller). The output buffer for that skipped call is not left
at the sentinel — something is written — but its value is not a fresh
computation for that sample position: at `offset=2594` it exactly reproduces
the nearby already-observed value from the prior block's start
(`outputL[2591]` and `outputL[2594]` both print `-0.00588441`), consistent
with the out-of-process proxy silently reusing/duplicating stale buffer
content rather than actually dispatching that request through to the
extension's `internalRenderBlock`. This project's own `internalRenderBlock`
was already confirmed (by direct source reading) to call `pullInputBlock`
unconditionally for every non-zero `frameCount` it receives — the only way
the pull is skipped is if the out-of-process XPC proxy never delivers that
call to the extension in the first place.

**Status: root-caused as far as this project's own visibility allows.** This
is a confirmed, reproducible, harness-independent defect in Apple's own
out-of-process AUv3 render-dispatch proxy: a render request immediately
following a `frameCount == 0` request is silently dropped/short-circuited
(no pull, no real processing, reused/stale output), while still reporting
success. It is not a defect in this project's DSP core or wrapper C++ — both
were read in full and confirmed to behave correctly for every call they
actually receive; the problem is calls that never arrive. No fix is proposed
or authorized in this project's own source, since there is nothing here to
fix — the located defect is in Apple's own hosting layer, one call boundary
above anything this project's `internalRenderBlock` can observe or control.
Confirming the exact internal XPC/proxy mechanism further (rather than just
its externally observable effect, already conclusively demonstrated here)
would need Apple's own Instruments/symbol-level tooling or an Apple Feedback
report, not further investigation from this side. HT-3 remains open pending
an owner decision on how to treat this: file Apple feedback, retest under
a newer iOS/Xcode in case it is a known/fixed issue, or record it as an
accepted external constraint. A real-host implication worth naming for the
owner: some hosts do legitimately issue zero-frame render callbacks (e.g.
during transport-stopped or otherwise idle states), so this is not purely an
artificial test-harness edge case — any host that happens to interleave a
zero-frame callback before a real one would hit this same silent corruption.

**Third-party AU cross-check (2026-09-21), answering "would switching this
project to JUCE avoid this":** rather than reason about it, the identical
pull-instrumentation probe (leading zero-frame call, then a real call,
checking whether `pullInputBlock` is invoked) was run against third-party
AUv3 extensions already installed on the same device — not built by this
project, no third-party source touched, using only the public
`AVAudioUnitComponentManager`/`AUAudioUnit` discovery APIs this project
already uses. New test `testThirdPartyZeroFrameCrossCheck` (added to
`PhysicalAcceptanceTests.mm`) first enumerated all 542 installed audio
components via a wildcard `componentsMatchingDescription:` query
(`testEnumerateInstalledAudioComponents`, also retained), then targeted:

- **Blackhole** (Eventide) — Eventide builds its own in-house DSP framework,
  not JUCE; a clean non-JUCE control.
- **Eos 2** (Audio Damage) — widely understood to ship iOS AUv3 ports built
  with JUCE.
- **AUDelay** (Apple) — a system AU, included as an in-process reference
  point, though its result below turned out not to be a clean comparison.

**Result: both third-party out-of-process AUv3s show the exact same
pull-skip at the exact same positions as Aetherfield's own AU** — a skipped
`pullInputBlock` invocation (with `status=0`/`noErr` returned regardless) at
`offset=0` (following the leading zero-frame call) and at `offset=2594`
(following the in-sequence wraparound zero-frame call), for both Blackhole
and Eos 2. AUDelay's result is not usable as a clean data point: nearly every
call after the first returned `status=-10876`, consistent with Apple's
built-in system AUs not being genuine out-of-process app-extension AUs in
the same sense and not supporting being driven this way at all — this is a
harness/target-mismatch artifact for that one probe, not a third finding.

**This settles the question the investigation was for.** The defect
reproduces identically on a completely independent, professionally-built,
explicitly non-JUCE plugin (Eventide's Blackhole), which rules out "specific
to this project's own AU implementation" and rules out "specific to JUCE"
in the same stroke — it is present regardless of the plugin's own framework.
**Switching Aetherfield's wrapper to JUCE would not avoid this defect**: JUCE
AUv3 builds are hosted through the identical OS-level out-of-process
app-extension/XPC render-dispatch mechanism, and a real JUCE-built plugin
(Audio Damage's Eos 2) already exhibits the identical failure. The defect is
confirmed to live in Apple's own out-of-process AU hosting layer, external to
any plugin framework choice; ADR-007's native-Apple-APIs-vs-JUCE comparison
is unaffected by this finding.

**Newer-iOS/Xcode retest: checked, already on latest, deferred (2026-09-21).**
Owner asked to retest on a newer iOS/Xcode in case this is already fixed
upstream. Checked before taking any action: this machine has exactly one
Xcode installed, version 27.0 (build 27A266a) — the current stable release,
bundling iOS SDK 27.0, with no beta tooling (`xcodes`, Xcode-beta.app)
present. The connected device (Josh's iPhone 16 Pro Max) is already running
iOS 27.0 (build 24A437), matching. `softwareupdate --list` on the host Mac
(currently macOS 26.6.2) offers a macOS point update (Tahoe 26.7) and a
macOS major upgrade (macOS 27), but Xcode itself does not update through
`softwareupdate` and neither upgrade guarantees a newer Xcode/iOS SDK
becomes available afterward — that is checked and obtained separately, from
the App Store or developer.apple.com. There is therefore no newer *stable*
Xcode/iOS combination reachable without either a macOS major upgrade
(~11.7GB, requires a restart) or enrolling in Apple's beta program (needs
the owner's own Apple ID sign-in and installs beta software on both the
daily-driver Mac and physical iPhone). Both carry real disruption/stability
risk to machines in everyday use, so neither was attempted without explicit
owner sign-off. **Owner decision (2026-09-21): defer.** This machine and
device are already on the latest generally-available release; no system
changes were made. Revisit this retest naturally once Apple ships a newer
stable point release, consistent with ADR-010's own "rolling policy,
re-verify at the time" precedent for iOS version state.

The 65536-signal/65536-zero-tail portion of the
partition, the observed-host-maximum coverage, raw-PCM retention, and the
explicit capacity-rejection probe remain separately open per Task 1's
checklist.

#### (b) Three build warnings: resolved, re-verified by rebuild, install unaffected

Baseline directly inspected (`PlistBuddy`) before fixing, not assumed:
`AetherfieldAUExtension`'s `CFBundleVersion`/`CFBundleShortVersionString`
were `1`/`1.0`; `AetherfieldHost` had neither key at all ("Does Not
Exist"), matching the mismatch warning's text exactly.

**Fix 1 (CFBundleVersion mismatch):** added `CURRENT_PROJECT_VERSION: "1"`
and `MARKETING_VERSION: "1.0"` to `AetherfieldHost`'s settings in
`project.yml`, matching the extension's actual observed values rather
than an invented scheme.

**Fix 2/3 (orientation + launch-storyboard):** both warnings' own text
says "unless the app requires full screen." The more honest single fix —
`INFOPLIST_KEY_UIRequiresFullScreen: YES`, since `AetherfieldHost` has no
UI and never will — was tried first, but surfaced a *new* warning on
rebuild: `UIRequiresFullScreen` is deprecated starting **iOS 26.0**, this
project's own deployment floor (ADR-010), "will be ignored in a future
release." Not usable as a durable fix. Reverted to the conventional pair:
`INFOPLIST_KEY_UISupportedInterfaceOrientations` (all four orientations,
Xcode's own default set) and `INFOPLIST_KEY_UILaunchScreen_Generation: YES`
(synthesizes an empty `UILaunchScreen` dict — no storyboard file needed,
supported since iOS 14+).

Rebuilt both configurations from clean, isolated `-derivedDataPath`s to
confirm the actual before/after warning state rather than trusting the
settings alone:

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldHost -configuration Release -allowProvisioningUpdates \
  -derivedDataPath <isolated path> build          # device: exit 0, 0 warnings

xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -target AetherfieldHost -configuration Release \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,id=112B34C4-7540-4828-B08E-AB8B8DC2A84D' \
  -derivedDataPath <isolated path> build           # simulator: exit 0, 0 warnings
```

All three warnings gone on both builds; the discarded `UIRequiresFullScreen`
attempt's deprecation warning is not present in the final state. Install
and registration were re-verified after the fix, not assumed safe from a
clean build alone (per this project's own prior lesson —
"builds cleanly" previously masked the missing-executable defect above):
`xcrun simctl install` on the dedicated simulator exited 0, and
`pluginkit -m` still reports
`com.aetherfield.placeholder.AetherfieldHost.AetherfieldAUExtension(1.0)`
registered. Post-fix `CFBundleVersion`/`CFBundleShortVersionString` were
inspected again directly: both bundles now `1`/`1.0`, matching.

### Task 1 portable baseline and blocked HT-3 rerun (2026-09-21)

The independent Task 1 baseline passed:

```sh
cmake -S . -B build/host-device-baseline -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-device-baseline --parallel
ctest --test-dir build/host-device-baseline --output-on-failure
bash scripts/check_dsp_source_drift.sh
```

Configure/build exited 0; CTest discovered and passed **8/8** suites,
including `aetherfield_hybrid_bypass_tests`; source drift exited 0 with
**7 files matched**.

The separate unsigned AU compile also exited 0 (`BUILD SUCCEEDED`):

```sh
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldAUExtension -configuration Debug \
  -destination 'generic/platform=iOS' \
  -derivedDataPath /private/tmp/aetherfield-unsigned-baseline \
  CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY='' CODE_SIGNING_REQUIRED=NO \
  clean build
```

This is compilation evidence only. The corrected HT-3 `{4096}` rerun remains
blocked: `xcrun simctl list devices available` reported CoreSimulatorService
connection refusal, and `xcrun devicectl list devices` timed out waiting for
CoreDeviceService. No new HT-3 conclusion is claimed; resume by restoring a
runnable destination and rerunning the focused corrected case.

A subsequent recovery check found no registered CoreSimulator/CoreDevice
launchd service, and `open -a Simulator` reported that the Simulator
application is unavailable on this machine. The runtime blocker therefore
remains external to the repository; no source or signing workaround was
attempted.

The follow-up harness build check reached the same boundary: both a normal
Simulator-scheme build and an explicit `generic/platform=iOS Simulator`
build failed before compilation because Xcode reported no supported scheme
destinations / no matching generic Simulator destination. This is not a
harness compile failure and does not change the prior corrected-harness
status.

**Runtime recovery diagnosis (2026-09-21):** the Xcode installation initially
contained the iOS 27 Simulator SDK but no runtime or `Simulator.app`. The
missing iOS 27.0 Simulator runtime was installed with
`xcodebuild -downloadPlatform iOS` (8.05 GB, completed successfully). CoreSim
still fails while mounting the runtime: its log reports a missing
personalization manifest and an unresponsive `simdiskimaged` service. A
user-scoped CoreSimulator/CoreDevice restart did not resolve it; the
system-owned `simdiskimaged` kickstart was denied by macOS. A reboot is now the
smallest remaining environment repair. No repository source or signing state
was changed.

## PLANNED validation gates after Phase 1

These gates describe future work. None is an executed reverb test, and none changes the Phase 0 reference WAV.

| Gate | Required evidence | Ownership |
|---|---|---|
| Delay/lifecycle skeleton | Exact impulse positions and wraparound; rejected invalid preparation; repeatable reset; all buffer extents respected; zero-frame calls safe; allocation occurs only during preparation | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S1" above** |
| Fixed late network | Independently computed matrix orthogonality and damping bounds; finite deterministic impulse/silence/noise renders; double-precision reference comparison; long zero-input decay; rate/block partition coverage. Concrete bounds NS-1…NS-11 are defined in ADR-003 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phases/phase1-s2-verification-plan.md | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S2" above** |
| Parameter transitions | Endpoints, invalid values, repeated retargeting and changing block partitions; no discontinuity from smoother state reset; signal-transition metrics plus audition; no allocation or blocking on render path. Concrete cases PT-1…PT-9 are defined in ADR-004 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phases/phase1-s2-verification-plan.md | **Satisfied 2026-09-17; see "IMPLEMENTED: Phase 1 parameter transitions" above** |
| Modulation experiment | Fixed baseline retained; endpoint/rate stress; interpolation boundary tests; measured decay and output growth under worst-case combinations; explicit approval before enabling | Sol reviews stability limits; Terra experiments; Luna reproduces |
| Sonic acceptance | Impulse and repeatable musical corpus; recorded peak/RMS/decay/stereo measurements; listening notes identifying ringing, onset density, width and unintended pitch movement | Terra prepares; owner auditions; Sol reviews consequences — **S2/PT impulse component prepared 2026-09-17 (not carried further). DS-B component: CLOSED 2026-09-18** after three listening rounds, DS-13's new per-channel measurement, and Sol's review — see "Round 3 and the DS-B Sonic acceptance gate: closed" above. Verdict: no architectural revision warranted; nothing reopens `N`, the delay set, or the tap design |

For all future tests: use named cases, print the failed condition, and return nonzero on failure even in Release. Preserve seeds, parameter settings, sample rates and block sequences with evidence. Missing measurements are **unmeasured**, never passes. Report output overshoots and safety interventions rather than hiding them by clipping or regenerating reference data. Exact byte comparisons apply only where the operation/toolchain contract supports them; nonlinear or transcendental implementations need justified numerical tolerances.

## Physical device acceptance (2026-09-21, Task 0/1 authorized)

**Device:** iPhone 16 Pro Max, iOS 27.0 (build 24A437) — newest-tier iPhone  
**Host:** AUM (capabilities: unmeasured)  
**Configuration:** Release, signed with Team ID W2VVZU52J6  
**Execution procedure:** [phase1-host-device-execution-procedure.md](phases/phase1-host-device-execution-procedure.md)  
**Run ID:** `20260922-023534-aa90ffa`  
**Manifest:** `artifacts/host-device/20260922-023534-aa90ffa/manifest.json`

### Test results

**Portable baseline (Release configuration):**
- CTest: 8/8 suites passed
- DSP source drift: 7 files matched
- Signing: Both AetherfieldAUExtension and AetherfieldHost Release signed successfully

**Physical device tests:**

| Test | Result | Duration | Notes |
|---|---|---|---|
| **HT-1: Lifecycle stress** | ✅ PASSED | 17s | 100 same-instance + 100 fresh instantiate/render/destroy cycles at both 44.1 kHz and 48 kHz; no crashes, timeouts, or resource exhaustion |
| **HT-3: Partitioned rendering** | ⚠️ FAILED (expected) | 4s | Bit-exact pass on fixed/ragged partitions at both rates; zero-frame partition set `{0,1,13,64,512,977,1024,3,0}` shows mid-stream mismatch (1193–1297 samples onset) at both rates — **confirmed as Apple out-of-process AU proxy defect (pull-skip after zero-frame call), verified cross-product with third-party plugins** |
| **Device matrix coverage** | Partial | — | 1 of 4 corners tested (newest-iPhone A18); oldest-iPhone A13, oldest-iPad A12, newest-iPad Pro unmeasured pending hardware availability |

**Owner decisions recorded (2026-09-21):**
- Zero-frame Apple defect: record as accepted external constraint (not product fix)
- Task 0 full authorization: all device corners, AUM primary host, Release signed builds, execution scope per evidence contract

No architectural revision, no further DSP core or wrapper implementation authorized by these results. HT-2/HT-4–6/HT-11–12 remain deferred pending full matrix or separate authorization.
