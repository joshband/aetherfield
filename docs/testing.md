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

## IN PROGRESS: Sonic acceptance — first impulse render (2026-09-17)

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

## PLANNED validation gates after Phase 1

These gates describe future work. None is an executed reverb test, and none changes the Phase 0 reference WAV.

| Gate | Required evidence | Ownership |
|---|---|---|
| Delay/lifecycle skeleton | Exact impulse positions and wraparound; rejected invalid preparation; repeatable reset; all buffer extents respected; zero-frame calls safe; allocation occurs only during preparation | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S1" above** |
| Fixed late network | Independently computed matrix orthogonality and damping bounds; finite deterministic impulse/silence/noise renders; double-precision reference comparison; long zero-input decay; rate/block partition coverage. Concrete bounds NS-1…NS-11 are defined in ADR-003 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phases/phase1-s2-verification-plan.md | **Satisfied 2026-09-16; see "IMPLEMENTED: Phase 1 S2" above** |
| Parameter transitions | Endpoints, invalid values, repeated retargeting and changing block partitions; no discontinuity from smoother state reset; signal-transition metrics plus audition; no allocation or blocking on render path. Concrete cases PT-1…PT-9 are defined in ADR-004 (decisions.md); the consolidated case list (build targets, dependency flags) is in docs/phases/phase1-s2-verification-plan.md | **Satisfied 2026-09-17; see "IMPLEMENTED: Phase 1 parameter transitions" above** |
| Modulation experiment | Fixed baseline retained; endpoint/rate stress; interpolation boundary tests; measured decay and output growth under worst-case combinations; explicit approval before enabling | Sol reviews stability limits; Terra experiments; Luna reproduces |
| Sonic acceptance | Impulse and repeatable musical corpus; recorded peak/RMS/decay/stereo measurements; listening notes identifying ringing, onset density, width and unintended pitch movement | Terra prepares; owner auditions; Sol reviews consequences — **impulse component prepared 2026-09-17, see "IN PROGRESS: Sonic acceptance" above; musical corpus, listening notes and Sol's review remain outstanding** |

For all future tests: use named cases, print the failed condition, and return nonzero on failure even in Release. Preserve seeds, parameter settings, sample rates and block sequences with evidence. Missing measurements are **unmeasured**, never passes. Report output overshoots and safety interventions rather than hiding them by clipping or regenerating reference data. Exact byte comparisons apply only where the operation/toolchain contract supports them; nonlinear or transcendental implementations need justified numerical tolerances.
