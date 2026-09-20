# AUv3 wrapper skeleton and parameter-event bridge — implementation task plan

**STATUS: Design/task plan only. Not implemented. Writing this document
authorizes no `.h`/`.cpp`/`.mm`/Xcode-project work; implementation is a
future, separately authorized step, exactly as `phase1-s1-plan.md`,
`phase1-s2-plan.md`, `phase1-pt-plan.md` and
`phase1-ds-integration-plan.md` were before their own implementations were
authorized.**

This is the **first bounded increment** of the wrapper work
`docs/roadmap.md`'s LATER section names as downstream of
[ADR-007](../decisions/ADR-007-auv3-integration-comparison.md)…
[ADR-012](../decisions/ADR-012-host-device-acceptance-catalog.md), all
six of which are accepted. Consistent with this project's established
pattern of small bounded increments (S1 → S2 → PT → DS-A → DS-B, never one
plan covering an entire milestone), this plan covers exactly two things:

1. **Task 1** — a shared DSP source-list manifest, closing
   [ADR-010](../decisions/ADR-010-device-lifecycle-matrix.md)'s named
   "source-list ownership" prerequisite (currently unmet: the DSP sources
   are inlined directly in `CMakeLists.txt`, not in a form a future Xcode
   target could share without copying).
2. **Task 2** — a portable, host-independent parameter-event bridge
   (`src/wrapper/`), implementing
   [ADR-008](../decisions/ADR-008-parameter-event-bridge.md)'s mailbox
   design and [ADR-008 §7](../decisions/ADR-008-parameter-event-bridge.md)'s
   reset-request flag, fully unit-testable today via the existing
   CMake/CTest loop — **no Xcode project, no Apple framework, no device**
   required for this task.
3. **Task 3** — a minimal native `AUAudioUnit` skeleton
   (`src/auv3/` + a new Xcode project) that wires Task 2's bridge to real
   host callbacks, maps [ADR-010](../decisions/ADR-010-device-lifecycle-matrix.md)'s
   lifecycle table onto `AUAudioUnit`, and implements only the
   already-*decided* parts of
   [ADR-009](../decisions/ADR-009-production-bus-policy.md) (stereo-in/
   stereo-out bus, sum-to-mono reduction, bit-exact dry passthrough on
   bypass) needed to get coherent stereo audio through a real host at all.

## Non-goals (read first)

This document is a design/task plan only, exactly as every prior
`phase1-*-plan.md` states of itself. It does **not** authorize:

- Writing or editing any `.h`/`.cpp`/`.mm`/`.plist`/Xcode-project file.
  Every design decision below is transcribed from an already-accepted ADR,
  not re-derived with different numbers, except where explicitly marked
  "**plan-level decision**" (file/directory layout and the two concrete
  choices ADR-008 left open for the implementation plan to make: the
  Bridge Controller's scheduling mechanism, and its polling/wake interval
  — see Task 3).
- [ADR-009](../decisions/ADR-009-production-bus-policy.md)'s hybrid
  bypass mechanism (the `T_silence(decay)`-bounded CPU-saving tail
  computation). This plan implements only ADR-009's already-decided,
  always-safe fallback — compute the wet path unconditionally and output
  bit-exact dry on bypass — never the optimization. The hybrid mechanism,
  its `getAll()`/`silenceBoundSamples_` wiring, and the "kill tail
  instantly" affordance remain a **separate, later plan**.
- [ADR-011](../decisions/ADR-011-state-schema.md)'s state schema, `fullState`
  persistence, the StateRestore producer role, or the
  `ParameterAutomation::getAll()`/`setAll()` obligations it names. No
  `fullState` override is added by this plan; a host reading state before
  this work lands gets whatever `AUAudioUnit`'s own default parameter-tree
  persistence provides, which is not evaluated here.
- Any device, host, or `auval` execution. **HT-1 through HT-12
  ([ADR-012](../decisions/ADR-012-host-device-acceptance-catalog.md))
  remain entirely unrun by this plan.** Task 2's own new test category
  (PB-1…PB-N, defined below) is a portable CTest-level check of the bridge
  *mechanism*, not a substitute for any HT category — see "What this does
  and does not close" below.
- UI implementation or toolkit choice (deferred per ADR-007). A host's own
  *generic* parameter view (which every AUv3 host provides automatically
  from an `AUParameterTree`, with no custom view required) is sufficient
  to exercise the UI producer role end-to-end; this plan relies on that,
  not on any Aetherfield-authored view.
- Signing, provisioning, bundle-identifier, or distribution decisions
  (explicitly deferred, ADR-007/ADR-010). Placeholder identifiers are used
  and named as placeholders.
- Any change to `src/dsp/`. This plan touches only `CMakeLists.txt` (Task
  1), new files under `src/wrapper/` (Task 2), and new files under
  `src/auv3/` plus a new Xcode project (Task 3).
- Re-verifying ADR-010's rolling iOS/iPadOS deployment floor or ADR-012's
  "newest available" device pair against Apple's current lineup. Both are
  explicitly named as rolling policies to be re-checked "at
  implementation-plan time" — **this plan is that time**, so Task 3 states
  the re-verification as its own first step rather than reusing the
  2026-09-18/19 snapshot silently.

## What this does and does not close

| Item | Before this plan | After this plan |
|---|---|---|
| ADR-010's source-list ownership convention | Not met (sources inlined in `CMakeLists.txt`) | Met (Task 1) |
| ADR-008's mailbox/Bridge Controller design | Designed, unimplemented | Implemented and unit-tested (Task 2) |
| ADR-008 §7's reset-request flag | Designed, unimplemented | Implemented and unit-tested (Task 2) |
| A running `AUAudioUnit` that a host can load | Does not exist | Exists (Task 3), **compiled, not yet auval-run or device-tested** |
| ADR-009's bus layout, sum-to-mono, dry passthrough | Decided, unimplemented | Implemented (Task 3), **hybrid bypass optimization still not implemented** |
| HT-1, HT-2, HT-6, HT-8 (ADR-012) | Named, unrun | Still unrun — this plan makes them *runnable for the first time*, it does not run them |
| HT-3, HT-4, HT-5, HT-7, HT-9…HT-12 | Named, unrun | Still unrun; several remain blocked on later plans (HT-5, HT-7) |
| PB-1…PB-N (this plan's own new CTest gate) | Does not exist | Implemented and passing, on macOS arm64 only — a portable mechanism check, not host/device evidence |

## Dependency and decision recap (read, not re-derived)

Everything below is quoted or paraphrased from already-accepted records;
this plan invents no new architecture. Read the source ADR before
implementing if any of this is unclear — this table is a locator, not a
substitute.

- **ADR-007**: native `AUAudioUnit` APIs, no JUCE, no Apple types in
  `src/dsp/`.
- **ADR-008 §1–§6**: a single non-render "Bridge Controller"; six mailbox
  cells (`{Decay, Damp, Mix} × {Host, UI}`, StateRestore's three cells
  superseded by ADR-011, out of scope here); render-thread intake does at
  most three wait-free mailbox writes per callback; fixed scan order
  Host-then-UI (a live UI touch wins over a same-drain Host value);
  `rampDurationSampleFrames` read and discarded; offline drain is
  synchronous, once per block, before `process()`.
- **ADR-008 §7** (this session's amendment): a wrapper-owned wait-free
  `resetRequested_` flag; `-reset` (any thread) stores `true`; the render
  thread exchanges-and-clears it at the top of every callback, before
  anything else, and calls `DiffusionStereoPath::reset()` there — never
  concurrently with `process()`.
- **ADR-009 §1/§3** (decided parts only): stereo-in/stereo-out bus,
  sum-to-mono reduction feeding the existing mono `DiffusionStereoPath`;
  `canProcessInPlace` recommended `true` pending implementation-time
  confirmation (Task 3 confirms it for what it actually builds, per
  ADR-009 §3's own instruction). **ADR-009 §2's hybrid bypass mechanism is
  explicitly out of scope**; this plan implements only bit-exact dry
  passthrough on `shouldBypassEffect`, always computing the wet path in
  full underneath it (the safe, unoptimized fallback ADR-009 itself
  describes as the alternative to the hybrid).
- **ADR-010**: sample rates `{48kHz, 44.1kHz}` only, `prepare()` rejects
  (never clamps) anything else; block size fully host-variable including
  zero-length, no new zero-length logic (DS-B Task 3's existing contract
  is reused unchanged); lifecycle mapping table
  (`allocateRenderResourcesAndReturnError:` → `prepare()`,
  `-reset` → `reset()` via ADR-008 §7's flag, per-block aggregate fault →
  DS-B Task 3's existing self-recovery, unchanged); deployment floor is
  "current major version minus one," **to be re-verified now, not reused
  from the 2026-09-18/19 snapshot** (Task 3, step 0); source-list
  ownership convention (Task 1).
- **ADR-012**: HT-1…HT-12 methodology (not evidence); device/chip-tier
  scope decided as per-family corners, "newest available" half **to be
  re-verified now** (Task 3, step 0); `auval` is a first-line structural
  check that substitutes for no HT category.
- **DS-B Task 3** (already implemented, unchanged by this plan):
  `DiffusionStereoPath::process(mono, left, right, count)` — mono in,
  stereo out; `count == 0` is a no-op that consumes nothing;
  `resetPending_` is checked and consumed at block entry before
  processing; the cumulative fault counter survives `reset()`.

## File layout (plan-level decision)

Two new top-level source trees, matching ADR-010's proposed convention
("a new, separate directory... created only when that work is separately
authorized... never modifies `src/dsp/` headers"):

```
src/
  dsp/               # unchanged — portable core, no Apple types
  wrapper/           # NEW — portable wrapper-support logic (Task 2).
                     # No Apple/Objective-C types. Builds and tests under
                     # the existing CMake/CTest loop like src/dsp/ does.
    ParameterBridge.h
    ParameterBridge.cpp
    ResetRequest.h
  auv3/              # NEW — Apple-only glue (Task 3). Never built by the
                     # portable CMake loop; only by the new Xcode project.
    AetherfieldAudioUnit.h
    AetherfieldAudioUnit.mm
    AetherfieldAudioUnitFactory.mm
    Info.plist
cmake/
  DspSources.cmake    # NEW — the single-sourced manifest (Task 1)
platform/
  apple/
    Aetherfield.xcodeproj/   # NEW — created via Xcode, not hand-authored (Task 3)
scripts/
  check_dsp_source_drift.sh # NEW — the drift check ADR-010 names (Task 1)
tests/
  ParameterBridgeTests.cpp  # NEW — PB-1…PB-N (Task 2)
```

`src/wrapper/` is deliberately **not** `src/auv3/`: the mailbox mechanism,
the reset flag, and the render-thread coalescing logic contain no Apple
types and no AUv3 concepts — they are a plain C++ producer/consumer
primitive that a future non-Apple integration (should one ever exist)
could reuse unchanged. Only the actual `AUAudioUnit` subclass, its factory,
and Apple-specific plumbing belong in `src/auv3/`. This split is what
makes Task 2 fully testable today without Xcode.

---

## Task 1 — Shared DSP source-list manifest

**Files:**
- Create: `cmake/DspSources.cmake`
- Modify: `CMakeLists.txt`
- Create: `scripts/check_dsp_source_drift.sh`

Closes ADR-010's named prerequisite: "The future Apple/Xcode target
consumes the *same* `src/dsp/` file list — not a copy — through one
explicit, single-sourced list... so the two build systems can never
independently drift." Today the list is inlined in `CMakeLists.txt`'s
`add_library(aetherfield_dsp STATIC ...)` call; there is nothing yet for
an Xcode project to read without copying it by hand.

- [ ] **Step 1: Extract the source list into its own file**

Create `cmake/DspSources.cmake`:

```cmake
# The single-sourced list of portable DSP core files. Both the CMake
# build and the Xcode project under platform/apple/ read this exact
# list (ADR-010, "Source-list ownership between the portable build and
# the future Apple build"). Never duplicate these paths elsewhere.
set(AETHERFIELD_DSP_SOURCES
    src/dsp/GainProcessor.cpp
    src/dsp/DelayLine.cpp
    src/dsp/FeedbackDelayNetwork.cpp
    src/dsp/ParameterAutomation.cpp
    src/dsp/SchroederAllpass.cpp
    src/dsp/DiffusionStereoPath.cpp
)
```

- [ ] **Step 2: Point `CMakeLists.txt` at it**

In `CMakeLists.txt`, replace:

```cmake
add_library(aetherfield_dsp STATIC
    src/dsp/GainProcessor.cpp
    src/dsp/DelayLine.cpp
    src/dsp/FeedbackDelayNetwork.cpp
    src/dsp/ParameterAutomation.cpp
    src/dsp/SchroederAllpass.cpp
    src/dsp/DiffusionStereoPath.cpp
)
```

with:

```cmake
include(cmake/DspSources.cmake)

add_library(aetherfield_dsp STATIC ${AETHERFIELD_DSP_SOURCES})
```

placed before the `add_library` call, at the top of the file alongside
the other `project()`/`set()` lines.

- [ ] **Step 3: Rebuild and confirm no behavior change**

Run: `rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: identical `6/6` (soon `7/7`, after Task 2) suite pass, byte-for-byte
the same as before this change — this step changes no source, only how
the existing list is spelled.

- [ ] **Step 4: Write the drift-check script**

Create `scripts/check_dsp_source_drift.sh`:

```bash
#!/usr/bin/env bash
# Fails if cmake/DspSources.cmake and the Xcode project's shared-file
# group for the DSP core name a different set of src/dsp/ paths.
# ADR-010: "a CI check (or, until CI exists, a recorded manual
# verification step)... that confirms the portable CMake target's
# source list and the Apple target's shared-file list are generated
# from, or diffed against, the same single manifest rather than
# maintained as two hand-edited lists."
set -euo pipefail

CMAKE_LIST=$(grep -oE 'src/dsp/[A-Za-z0-9_]+\.cpp' cmake/DspSources.cmake | sort -u)

XCODE_PROJECT="platform/apple/Aetherfield.xcodeproj/project.pbxproj"
if [[ ! -f "$XCODE_PROJECT" ]]; then
    echo "SKIP: $XCODE_PROJECT does not exist yet (Task 3 not implemented)."
    exit 0
fi

XCODE_LIST=$(grep -oE 'src/dsp/[A-Za-z0-9_]+\.cpp' "$XCODE_PROJECT" | sort -u)

if [[ "$CMAKE_LIST" != "$XCODE_LIST" ]]; then
    echo "FAIL: cmake/DspSources.cmake and $XCODE_PROJECT disagree on the DSP source list."
    diff <(echo "$CMAKE_LIST") <(echo "$XCODE_LIST") || true
    exit 1
fi

echo "OK: DSP source lists match ($(echo "$CMAKE_LIST" | wc -l | tr -d ' ') files)."
```

Run: `chmod +x scripts/check_dsp_source_drift.sh && ./scripts/check_dsp_source_drift.sh`
Expected before Task 3 exists: `SKIP: ... does not exist yet` (exit 0).
Expected after Task 3: `OK: DSP source lists match (6 files).` This script
has no CI wiring yet (this project has none); running it manually before
each commit that touches either list is the "recorded manual verification
step" ADR-010 names as acceptable pending real CI.

- [ ] **Step 5: Commit**

```bash
git add cmake/DspSources.cmake CMakeLists.txt scripts/check_dsp_source_drift.sh
git commit -m "Extract shared DSP source-list manifest (ADR-010)"
```

---

## Task 2 — Portable parameter-event bridge (`src/wrapper/`)

**Files:**
- Create: `src/wrapper/ResetRequest.h`
- Create: `src/wrapper/ParameterBridge.h`
- Create: `src/wrapper/ParameterBridge.cpp`
- Create: `tests/ParameterBridgeTests.cpp`
- Modify: `CMakeLists.txt`

This is ADR-008 §1–§6 and ADR-008 §7, implemented as plain portable C++
with no Apple dependency, mirroring `ParameterAutomation`'s own existing
relaxed-value/release-generation idiom exactly (ADR-008 §2: "identical in
shape to `ParameterAutomation::publish()`'s own already-accepted
pattern").

### Step 1: Write `ResetRequest.h` (no test needed — it is three lines of
already-standard atomic idiom, directly exercised by PB-8 below)

```cpp
#pragma once

#include <atomic>

namespace aetherfield::wrapper {

// ADR-008 section 7: a wrapper-owned wait-free flag closing ADR-010's
// named host-reset-concurrency gap. A host's -reset (any thread) calls
// requestFromAnyThread(). The render thread, and only the render thread,
// calls consumeIfPending() at the very start of every callback, before
// anything else; if it returns true, the render thread (and only the
// render thread) then calls DiffusionStereoPath::reset() itself. This
// class never touches DiffusionStereoPath: it is a pure signal.
class ResetRequest {
public:
    void requestFromAnyThread() noexcept {
        requested_.store(true, std::memory_order_release);
    }

    // Not idempotent to call from multiple threads: only the single
    // render thread may call this, matching every other render-thread-
    // only method in this codebase (ADR-004 (d)'s single-writer/single-
    // reader discipline, generalized here to single-consumer).
    bool consumeIfPending() noexcept {
        return requested_.exchange(false, std::memory_order_acquire);
    }

private:
    std::atomic<bool> requested_ {false};
};

} // namespace aetherfield::wrapper
```

- [ ] **Step 2: Write the failing test for `ParameterBridge`'s basic
  single-producer path**

Create `tests/ParameterBridgeTests.cpp`:

```cpp
#include "wrapper/ParameterBridge.h"

#include "dsp/DiffusionStereoPath.h"

#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

aetherfield::dsp::DiffusionStereoConfig validConfig(double sampleRate) {
    return {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0,
        .t60PiSeconds = 4.0,
        .dMaxDb = 48.0,
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180340052,
    };
}

// PB-1: a single Host write, drained once, is applied exactly once.
int testHostWriteAppliedOnDrain() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-1 fixture preparation failed");

    ParameterBridge bridge;
    bridge.writeHost(Parameter::Decay, 0.75F);
    const std::size_t applied = bridge.drain(path);
    if (applied != 1) return fail("PB-1 expected exactly one parameter applied");

    // Confirm it actually reached ParameterAutomation: render past the
    // shortest line's circulation and confirm output differs from the
    // Decay=0.5 default (same technique PT-1 already uses: compare
    // against a reference held at a different setting).
    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> left(impulseInput.size());
    std::vector<float> right(impulseInput.size());
    path.process(impulseInput.data(), left.data(), right.data(), impulseInput.size());

    aetherfield::dsp::DiffusionStereoPath reference;
    if (!reference.prepare(validConfig(48000.0))) return fail("PB-1 reference fixture preparation failed");
    std::vector<float> referenceLeft(impulseInput.size());
    std::vector<float> referenceRight(impulseInput.size());
    reference.process(impulseInput.data(), referenceLeft.data(), referenceRight.data(), impulseInput.size());

    if (left == referenceLeft && right == referenceRight) {
        return fail("PB-1 drained Host write did not change automation state");
    }
    std::cout << "PB-1 Host write applied through drain(): output diverged from Decay=0.5 default\n";
    return 0;
}

} // namespace

int main() {
    if (testHostWriteAppliedOnDrain() != 0) return 1;
    std::cout << "ParameterBridge tests passed\n";
    return 0;
}
```

- [ ] **Step 3: Run it to confirm it fails to compile (the header doesn't
  exist yet)**

Run: `g++ -std=c++20 -Isrc -c tests/ParameterBridgeTests.cpp -o /dev/null`
Expected: FAIL with `'wrapper/ParameterBridge.h' file not found`

- [ ] **Step 4: Write `ParameterBridge.h`**

```cpp
#pragma once

#include "dsp/DiffusionStereoPath.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace aetherfield::wrapper {

// The three controls this bridge carries, in the same order and meaning
// as DiffusionStereoPath::setDecay/setDamp/setMix (ADR-004). StateRestore
// is deliberately absent: ADR-011 superseded its mailbox cells with a
// separate atomic triple-buffer, out of scope for this plan.
enum class Parameter : std::size_t { Decay = 0, Damp = 1, Mix = 2 };
inline constexpr std::size_t kParameterCount = 3;

// One (parameter, producer) mailbox cell (ADR-008 section 2): a fixed
// atomic value plus a release-published generation counter, identical in
// shape to ParameterAutomation::publish()'s own transport. Not intended
// for direct use outside ParameterBridge; public only so ParameterBridge
// can be an aggregate of plain members with no heap allocation anywhere.
struct MailboxCell {
    std::atomic<float> value {0.0F};
    std::atomic<std::uint64_t> generation {0};
};

// ADR-008's Bridge Controller state: six fixed mailbox cells (Host x 3,
// UI x 3) and the single-writer drain-and-apply step. writeHost()/writeUi()
// are wait-free and callable from any thread, including the render thread
// (for Host, per ADR-008 section 1's render-thread intake). drain() must
// never be called concurrently with itself -- it is the one Bridge
// Controller task ADR-008 section 1 requires -- and is the only thing in
// this class allowed to call DiffusionStereoPath::setDecay/setDamp/setMix.
class ParameterBridge {
public:
    ParameterBridge() noexcept;

    // Wait-free: one relaxed float store, one release generation
    // fetch_add. Safe from any thread. Safe to call concurrently with
    // drain() and with writes to OTHER (parameter, role) cells. Not safe
    // to call concurrently with another writeHost()/writeUi() call to the
    // SAME (parameter, role) cell -- each role is one logical producer,
    // per ADR-008's own model.
    void writeHost(Parameter parameter, float normalizedValue) noexcept;
    void writeUi(Parameter parameter, float normalizedValue) noexcept;

    // The Bridge Controller's drain-and-apply step (ADR-008 sections 1,
    // 2, 5): for each parameter with a pending value from either role,
    // applies at most one call to the corresponding DiffusionStereoPath
    // setter, using the fixed scan order Host-then-UI so a same-drain UI
    // write always wins over a same-drain Host write (ADR-008 section 2).
    // Must never run concurrently with itself. Returns the number of
    // parameters actually applied (0..3), for test/diagnostic use only.
    std::size_t drain(aetherfield::dsp::DiffusionStereoPath& path) noexcept;

private:
    std::array<MailboxCell, kParameterCount> hostCells_;
    std::array<MailboxCell, kParameterCount> uiCells_;
    std::array<std::uint64_t, kParameterCount> consumedHostGeneration_ {};
    std::array<std::uint64_t, kParameterCount> consumedUiGeneration_ {};
};

// ADR-008 section 3's render-thread-side intake coalescing, factored out
// so it is testable without any Apple type: given a batch of (parameter,
// value) events observed in one render callback, in host-delivered
// (time) order, writes at most one Host mailbox update per distinct
// parameter that appeared, using each parameter's LAST event in the
// batch. The Apple-only render block (src/auv3/) is responsible only for
// translating AURenderEvent/AUParameterEvent into HostParameterEvent and
// calling this once per callback; it must never call writeHost() itself
// per individual event.
struct HostParameterEvent {
    Parameter parameter;
    float normalizedValue;
};

void applyHostEvents(ParameterBridge& bridge, const HostParameterEvent* events, std::size_t count) noexcept;

} // namespace aetherfield::wrapper
```

- [ ] **Step 5: Write `ParameterBridge.cpp`**

```cpp
#include "wrapper/ParameterBridge.h"

namespace aetherfield::wrapper {

ParameterBridge::ParameterBridge() noexcept = default;

void ParameterBridge::writeHost(Parameter parameter, float normalizedValue) noexcept {
    const std::size_t index = static_cast<std::size_t>(parameter);
    hostCells_[index].value.store(normalizedValue, std::memory_order_relaxed);
    hostCells_[index].generation.fetch_add(1, std::memory_order_release);
}

void ParameterBridge::writeUi(Parameter parameter, float normalizedValue) noexcept {
    const std::size_t index = static_cast<std::size_t>(parameter);
    uiCells_[index].value.store(normalizedValue, std::memory_order_relaxed);
    uiCells_[index].generation.fetch_add(1, std::memory_order_release);
}

namespace {

bool applySetter(aetherfield::dsp::DiffusionStereoPath& path, Parameter parameter, float value) noexcept {
    switch (parameter) {
        case Parameter::Decay: return path.setDecay(value);
        case Parameter::Damp: return path.setDamp(value);
        case Parameter::Mix: return path.setMix(value);
    }
    return false;
}

} // namespace

std::size_t ParameterBridge::drain(aetherfield::dsp::DiffusionStereoPath& path) noexcept {
    std::size_t applied = 0;
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        bool hasPending = false;
        float winningValue = 0.0F;

        const std::uint64_t hostGeneration = hostCells_[index].generation.load(std::memory_order_acquire);
        if (hostGeneration != consumedHostGeneration_[index]) {
            winningValue = hostCells_[index].value.load(std::memory_order_relaxed);
            consumedHostGeneration_[index] = hostGeneration;
            hasPending = true;
        }

        // UI is scanned second: if both roles have a pending value this
        // drain, the UI value overwrites winningValue and is what gets
        // applied -- ADR-008 section 2's "a live UI touch always wins
        // over a same-pass Host... value."
        const std::uint64_t uiGeneration = uiCells_[index].generation.load(std::memory_order_acquire);
        if (uiGeneration != consumedUiGeneration_[index]) {
            winningValue = uiCells_[index].value.load(std::memory_order_relaxed);
            consumedUiGeneration_[index] = uiGeneration;
            hasPending = true;
        }

        if (hasPending) {
            applySetter(path, static_cast<Parameter>(index), winningValue);
            ++applied;
        }
    }
    return applied;
}

void applyHostEvents(ParameterBridge& bridge, const HostParameterEvent* events, std::size_t count) noexcept {
    if (count == 0) return;

    bool seen[kParameterCount] = {false, false, false};
    float lastValue[kParameterCount] = {0.0F, 0.0F, 0.0F};

    // One pass, O(count): for each of the three parameters, remember only
    // the LAST event targeting it in this batch (ADR-008 section 3, step
    // 1 -- "overwriting any value already recorded there for this same
    // callback... No atomic operation, no allocation").
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = static_cast<std::size_t>(events[i].parameter);
        seen[index] = true;
        lastValue[index] = events[i].normalizedValue;
    }

    // At most three wait-free mailbox writes total, regardless of count
    // (ADR-008 section 3, step 2).
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        if (seen[index]) {
            bridge.writeHost(static_cast<Parameter>(index), lastValue[index]);
        }
    }
}

} // namespace aetherfield::wrapper
```

- [ ] **Step 6: Wire the new library and test target into `CMakeLists.txt`**

Add, after the existing `aetherfield_dsp` library block:

```cmake
add_library(aetherfield_wrapper STATIC
    src/wrapper/ParameterBridge.cpp
)
target_include_directories(aetherfield_wrapper PUBLIC src)
target_link_libraries(aetherfield_wrapper PUBLIC aetherfield_dsp)

add_executable(aetherfield_wrapper_tests tests/ParameterBridgeTests.cpp)
target_link_libraries(aetherfield_wrapper_tests PRIVATE aetherfield_wrapper)
```

Add `aetherfield_wrapper` and `aetherfield_wrapper_tests` to the existing
`-Wall -Wextra -Wpedantic -Werror` `foreach(target ...)` loop, and add:

```cmake
add_test(NAME aetherfield_wrapper_tests COMMAND aetherfield_wrapper_tests)
```

alongside the other `add_test` lines.

- [ ] **Step 7: Run it to confirm PB-1 passes**

Run: `rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: `7/7` suites pass, including `aetherfield_wrapper_tests`, printing
`PB-1 Host write applied through drain(): output diverged from Decay=0.5 default`.

- [ ] **Step 8: Commit**

```bash
git add src/wrapper CMakeLists.txt tests/ParameterBridgeTests.cpp
git commit -m "Implement PB-1: single Host write applied through ParameterBridge::drain()"
```

- [ ] **Step 9: Add PB-2 — UI write wins over a same-drain Host write**

Add to `tests/ParameterBridgeTests.cpp`, before `main()`:

```cpp
// PB-2: ADR-008 section 2's tie-break -- if both Host and UI have a
// pending value for the same parameter at drain time, UI wins, because
// it is scanned second and unconditionally overwrites.
int testUiWinsOverSameDrainHostWrite() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath hostOnly;
    aetherfield::dsp::DiffusionStereoPath both;
    if (!hostOnly.prepare(validConfig(48000.0)) || !both.prepare(validConfig(48000.0))) {
        return fail("PB-2 fixture preparation failed");
    }

    ParameterBridge hostOnlyBridge;
    hostOnlyBridge.writeHost(Parameter::Damp, 0.20F);
    hostOnlyBridge.drain(hostOnly);

    ParameterBridge bothBridge;
    bothBridge.writeHost(Parameter::Damp, 0.20F);
    bothBridge.writeUi(Parameter::Damp, 0.90F);
    const std::size_t applied = bothBridge.drain(both);
    if (applied != 1) return fail("PB-2 expected exactly one parameter applied, not one call per role");

    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> hostOnlyLeft(impulseInput.size());
    std::vector<float> hostOnlyRight(impulseInput.size());
    hostOnly.process(impulseInput.data(), hostOnlyLeft.data(), hostOnlyRight.data(), impulseInput.size());

    std::vector<float> bothLeft(impulseInput.size());
    std::vector<float> bothRight(impulseInput.size());
    both.process(impulseInput.data(), bothLeft.data(), bothRight.data(), impulseInput.size());

    if (hostOnlyLeft == bothLeft && hostOnlyRight == bothRight) {
        return fail("PB-2 UI value did not win: output matches Host-only Damp=0.20, expected UI's 0.90");
    }
    std::cout << "PB-2 UI write won over same-drain Host write, as ADR-008 section 2 requires\n";
    return 0;
}
```

Add `if (testUiWinsOverSameDrainHostWrite() != 0) return 1;` to `main()`
between the PB-1 call and the final `std::cout`.

Run: `cmake --build build --parallel && ./build/aetherfield_wrapper_tests`
Expected: both PB-1 and PB-2 print pass lines; exit 0.

- [ ] **Step 10: Commit**

```bash
git add tests/ParameterBridgeTests.cpp
git commit -m "Add PB-2: UI write wins over a same-drain Host write"
```

- [ ] **Step 11: Add PB-3 — a stale Host value from an earlier pass never
  wins over a fresh drain with nothing new pending**

```cpp
// PB-3: a Host write already consumed by a prior drain() must not be
// re-applied by a later drain() that has no new pending value -- the
// generation counter, not the mailbox's mere existence, gates
// re-application (mirrors ParameterAutomation::checkForNewTargets()'s
// own "if unchanged since consumedGeneration_, do nothing" contract).
int testConsumedHostWriteIsNotReapplied() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-3 fixture preparation failed");

    ParameterBridge bridge;
    bridge.writeHost(Parameter::Mix, 0.10F);
    if (bridge.drain(path) != 1) return fail("PB-3 first drain should apply exactly one parameter");
    if (bridge.drain(path) != 0) return fail("PB-3 second drain with nothing new pending should apply zero");
    std::cout << "PB-3 a consumed Host write is not reapplied on a subsequent empty drain\n";
    return 0;
}
```

Wire into `main()`, rebuild, confirm pass, commit
(`git commit -m "Add PB-3: consumed writes are not reapplied"`), same
shape as Steps 9–10. Repeat this Step-9/10 shape for the remaining cases
below; each is independent and does not need its own prose block.

- [ ] **Step 12: Add PB-4 — `applyHostEvents` coalesces a burst to the
  last value per parameter, at most three mailbox writes total**

```cpp
// PB-4: ADR-008 section 3 -- a burst of N events targeting the same
// parameter within one callback collapses to exactly one mailbox write,
// carrying the LAST event's value, before it ever reaches drain().
int testApplyHostEventsCoalescesToLastValuePerParameter() {
    using aetherfield::wrapper::HostParameterEvent;
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-4 fixture preparation failed");

    const HostParameterEvent events[] = {
        {Parameter::Decay, 0.10F},
        {Parameter::Decay, 0.20F},
        {Parameter::Decay, 0.95F}, // last Decay event in the batch: this one must win
        {Parameter::Mix, 0.40F},
    };

    ParameterBridge bridge;
    applyHostEvents(bridge, events, std::size(events));
    const std::size_t applied = bridge.drain(path);
    if (applied != 2) return fail("PB-4 expected exactly two parameters applied (Decay, Mix), not four");

    aetherfield::dsp::DiffusionStereoPath reference;
    if (!reference.prepare(validConfig(48000.0))) return fail("PB-4 reference fixture preparation failed");
    reference.setDecay(0.95);
    reference.setMix(0.40);

    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> left(impulseInput.size());
    std::vector<float> right(impulseInput.size());
    path.process(impulseInput.data(), left.data(), right.data(), impulseInput.size());

    std::vector<float> referenceLeft(impulseInput.size());
    std::vector<float> referenceRight(impulseInput.size());
    reference.process(impulseInput.data(), referenceLeft.data(), referenceRight.data(), impulseInput.size());

    if (left != referenceLeft || right != referenceRight) {
        return fail("PB-4 coalesced result did not match directly setting the LAST batch values");
    }
    std::cout << "PB-4 applyHostEvents() coalesced a 4-event burst to 2 mailbox writes, last value per parameter\n";
    return 0;
}
```

- [ ] **Step 13: Add PB-5 — `applyHostEvents` with an empty batch performs
  zero mailbox writes**

```cpp
// PB-5: a zero-length event batch (a callback in which the host delivered
// no AUParameterEvents at all -- the common case) must not touch any
// mailbox cell or generation counter.
int testApplyHostEventsEmptyBatchIsNoOp() {
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-5 fixture preparation failed");

    ParameterBridge bridge;
    applyHostEvents(bridge, nullptr, 0);
    if (bridge.drain(path) != 0) return fail("PB-5 expected zero parameters applied after an empty batch");
    std::cout << "PB-5 an empty event batch produces zero mailbox writes\n";
    return 0;
}
```

- [ ] **Step 14: Add PB-6 — `drain()` before any write is a no-op**

```cpp
// PB-6: draining a freshly constructed ParameterBridge, with nothing
// ever written to any cell, must apply zero parameters and must not
// crash or read uninitialized generation state.
int testDrainBeforeAnyWriteIsNoOp() {
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-6 fixture preparation failed");

    ParameterBridge bridge;
    if (bridge.drain(path) != 0) return fail("PB-6 expected zero parameters applied before any write");
    std::cout << "PB-6 draining before any write applies zero parameters\n";
    return 0;
}
```

- [ ] **Step 15: Add PB-7 — no allocation across a sustained
  write/drain cycle**

Mirror this project's existing allocation-override technique
(`tests/SchroederAllpassTests.cpp`'s `allocateOrThrow`/
`allocateAlignedOrThrow` global `operator new`/`operator new[]`
overrides, and PT-8's "zero allocation across... a change-every-sample
render" precedent) rather than reinventing one:

```cpp
// PB-7: sustained writeHost()/writeUi()/drain() calls allocate nothing,
// matching PT-8's existing zero-allocation precedent for the underlying
// setDecay/setDamp/setMix path and ADR-008 section 3's "no allocation"
// render-thread-work bound.
int testNoAllocationDuringWriteAndDrain() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-7 fixture preparation failed");
    ParameterBridge bridge;

    const std::size_t before = allocationCount; // reuse this file's own global counter, defined the same way SchroederAllpassTests.cpp defines it
    for (int i = 0; i < 1000; ++i) {
        bridge.writeHost(Parameter::Decay, 0.01F * static_cast<float>(i % 100));
        bridge.writeUi(Parameter::Damp, 0.01F * static_cast<float>(i % 100));
        bridge.drain(path);
    }
    const std::size_t after = allocationCount;
    if (after != before) return fail("PB-7 write/drain cycle allocated");
    std::cout << "PB-7 allocation delta through 1000 write/drain cycles: " << (after - before) << '\n';
    return 0;
}
```

This step additionally requires copying `tests/SchroederAllpassTests.cpp`'s
`allocateOrThrow`/`allocateAlignedOrThrow`/global-`operator new` override
block (and its `allocationCount` counter) into
`tests/ParameterBridgeTests.cpp` verbatim — it is test infrastructure this
project already has, not a new pattern to design.

- [ ] **Step 16: Add PB-8 — `ResetRequest` round trip**

```cpp
#include "wrapper/ResetRequest.h"

// PB-8: a request from a simulated "other thread" is observed and
// cleared by exactly one consumeIfPending() call; a second call with
// nothing new requested returns false.
int testResetRequestRoundTrip() {
    aetherfield::wrapper::ResetRequest request;
    if (request.consumeIfPending()) return fail("PB-8 expected no pending request before any requestFromAnyThread()");
    request.requestFromAnyThread();
    if (!request.consumeIfPending()) return fail("PB-8 expected a pending request after requestFromAnyThread()");
    if (request.consumeIfPending()) return fail("PB-8 expected the request to be cleared after one consumeIfPending()");
    std::cout << "PB-8 ResetRequest round trip: set once, consumed once, cleared\n";
    return 0;
}
```

- [ ] **Step 17: Wire PB-4 through PB-8 into `main()`, rebuild, confirm
  all eight print pass lines, commit**

```bash
git add tests/ParameterBridgeTests.cpp
git commit -m "Add PB-4..PB-8: coalescing, empty-batch, pre-write drain, allocation, reset-flag cases"
```

- [ ] **Step 18: Update `docs/start-here.md` and `docs/testing.md`**

Add a line to `docs/start-here.md`'s "What is implemented" list, matching
the existing bullet style:

> - **Wrapper parameter bridge (`src/wrapper/`):** `ParameterBridge` and
>   `ResetRequest`, implementing ADR-008 §1–§7's mailbox and reset-flag
>   design; PB-1 through PB-8 pass under
>   `aetherfield_wrapper_tests`. This is a portable, host-independent
>   mechanism check only — no AUv3 host has run this code; see
>   `phase1-wrapper-skeleton-plan.md`.

Add a `### PB-1…PB-8 — parameter bridge mechanism` section to
`docs/testing.md`, in the same style as the existing `### DS-10`/`### PT`
sections, recording the actual `ctest`/binary output once Task 2 is
implemented (this plan does not fabricate that evidence in advance).

---

## Task 3 — Minimal native `AUAudioUnit` skeleton (`src/auv3/`)

**Files:**
- Create: `src/auv3/AetherfieldAudioUnit.h`
- Create: `src/auv3/AetherfieldAudioUnit.mm`
- Create: `src/auv3/AetherfieldAudioUnitFactory.mm`
- Create: `src/auv3/Info.plist`
- Create: `platform/apple/Aetherfield.xcodeproj/` (via Xcode, not hand-authored)

This task requires Xcode and cannot be driven by the portable CMake/CTest
loop; it produces no new CTest target. Its own verification (below) is
therefore narrower and more honestly scoped than Tasks 1–2's.

### Step 0: Re-verify the two rolling policies before writing any code

Both ADR-010 and ADR-012 name their own numbers as **rolling policies**,
current only as of their 2026-09-18/19 acceptance, and both explicitly
say "re-verify... at implementation-plan time." This plan is that time.

- [ ] Re-check Apple's current major OS version and recompute "current
  major version minus one" (ADR-010's floor was iOS/iPadOS 26+, computed
  when iOS 27 was current; confirm whether a newer major has since
  shipped and recompute if so).
- [ ] Re-check Apple's current device lineup for the "newest available"
  half of ADR-012's two device pairs (iPhone and iPad Pro corners; the
  oldest-corner devices — iPhone SE 2nd gen/A13, iPad 8th gen/A12 — are
  fixed by the OS floor, not by "newest available," and do not need
  re-checking).
- [ ] Record both findings, dated, as a short addendum to ADR-010's
  "Correction note" section and ADR-012's device/chip-tier decision,
  following this project's own established correction-note convention —
  **do this as a small documentation update before writing Step 1's
  code**, not as an afterthought once the Xcode project exists.

### Step 1: Create the Xcode project

**Amendment (2026-09-20):** this session's environment has Xcode 27.0 but is
headless (a CLI agent session, no GUI automation available for Xcode's "New
Project" wizard) and has no `xcodegen` pre-installed. Rather than hand-editing
a raw `.pbxproj` (a fragile, easy-to-corrupt format with no way to validate
short of a full build, and a poor fit for code review — a reviewer cannot
meaningfully diff binary-plist-shaped project internals), the owner
authorized installing `xcodegen` (`brew install xcodegen`) and generating the
project from a small, human-readable, reviewable YAML spec instead. This is
the same "not hand-authored" principle the plan already stated, satisfied by
a different concrete tool than originally named (Xcode GUI vs. `xcodegen`) —
no requirement changes, only the mechanism producing the `.xcodeproj`.

- Install: `brew install xcodegen` (one-time, dev-tool-only; not a product
  dependency, nothing links against it, and it produces no runtime artifact
  — it only generates the `.xcodeproj` file).
- Write `platform/apple/project.yml` (the xcodegen spec — kept in version
  control; the generated `.xcodeproj` itself may also be committed for
  convenience, but `project.yml` is the source of truth for regeneration):

```yaml
name: Aetherfield
options:
  bundleIdPrefix: com.aetherfield.placeholder
targets:
  AetherfieldAUExtension:
    type: app-extension
    platform: iOS
    deploymentTarget: "REPLACE_WITH_STEP_0_FLOOR"
    sources:
      - path: ../../src/dsp
        excludes: ["*.h"]
      - path: ../../src/wrapper
        excludes: ["*.h"]
      - path: ../../src/auv3
    settings:
      HEADER_SEARCH_PATHS: ["$(SRCROOT)/../../src"]
      CLANG_CXX_LANGUAGE_STANDARD: "c++20"
      PRODUCT_BUNDLE_IDENTIFIER: com.aetherfield.placeholder.AetherfieldAUExtension
    info:
      path: ../../src/auv3/Info.plist
    frameworks:
      - AudioToolbox.framework
      - AVFoundation.framework
  AetherfieldHost:
    type: application
    platform: iOS
    deploymentTarget: "REPLACE_WITH_STEP_0_FLOOR"
    sources: []
    settings:
      PRODUCT_BUNDLE_IDENTIFIER: com.aetherfield.placeholder.AetherfieldHost
    dependencies:
      - target: AetherfieldAUExtension
        embed: true
```

(Apple requires every AUv3 extension to ship embedded in a container app;
`AetherfieldHost` exists only to satisfy that requirement and has no
functionality of its own beyond embedding the extension. Replace both
`REPLACE_WITH_STEP_0_FLOOR` placeholders with the actual number Step 0
re-verifies — do not hardcode "26.0" without having actually re-run Step 0.
`com.aetherfield.placeholder.*` bundle identifiers are **explicitly
placeholders**, named as such in a comment at the top of `project.yml`;
ADR-007/ADR-010 both defer the real identifier decision to the owner.
Signing is left at xcodegen's default (automatic, development-team-less) for
now.)

- Run `cd platform/apple && xcodegen generate` to produce
  `platform/apple/Aetherfield.xcodeproj`.
- This sources every file in `cmake/DspSources.cmake`'s list (Task 1) plus
  every `.cpp` in `src/wrapper/` plus everything in `src/auv3/` directly
  from `project.yml`'s `sources:` list — the header search path
  (`$(SRCROOT)/../../src`) matches the CMake target's own
  `target_include_directories(... PUBLIC src)`, so both build systems
  resolve `#include "dsp/..."` / `#include "wrapper/..."` identically.

### Step 2: `AetherfieldAudioUnit.h` — the subclass interface

```objc
#pragma once

#import <AudioToolbox/AudioToolbox.h>

// ADR-007: native AUAudioUnit, no JUCE. Owns exactly one
// aetherfield::dsp::DiffusionStereoPath and one
// aetherfield::wrapper::ParameterBridge (ADR-008) plus one
// aetherfield::wrapper::ResetRequest (ADR-008 section 7). No Apple or
// Objective-C type crosses into src/dsp/ or src/wrapper/ -- this header
// and its .mm are the only place that boundary is bridged.
@interface AetherfieldAudioUnit : AUAudioUnit
@end
```

### Step 3: `AetherfieldAudioUnit.mm` — lifecycle, parameter tree, render
block, reset

This is the substantial file. Each piece below maps directly to a row of
ADR-010's lifecycle table or a section of ADR-008; the comments name
which.

```objc
#import "AetherfieldAudioUnit.h"

#include "dsp/DiffusionStereoPath.h"
#include "wrapper/ParameterBridge.h"
#include "wrapper/ResetRequest.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::dsp::DiffusionStereoPath;
using aetherfield::wrapper::HostParameterEvent;
using aetherfield::wrapper::Parameter;
using aetherfield::wrapper::ParameterBridge;
using aetherfield::wrapper::ResetRequest;

@interface AetherfieldAudioUnit () {
    // Owned C++ state. Constructed once in -initWithComponentDescription:
    // and destroyed in dealloc; NOT reconstructed across a
    // deallocateRenderResources/reallocate cycle (ADR-010's named gap:
    // this plan's chosen resolution is "keep the instance alive," which
    // preserves the cumulative fault counter across that cycle without
    // needing to relocate it anywhere -- the simpler of ADR-010's two
    // named options, and the one that does not require inventing a new
    // wrapper-owned counter).
    std::unique_ptr<DiffusionStereoPath> _path;
    std::unique_ptr<ParameterBridge> _bridge;
    std::unique_ptr<ResetRequest> _resetRequest;

    // The last normalized value this AUParameter was set to, for
    // implementorValueProvider to read back. Not a substitute for
    // ADR-011's ParameterAutomation::getAll() (out of scope here) --
    // just what implementorValueProvider needs to answer host/UI reads
    // without touching the render-thread-only automation state.
    std::atomic<float> _lastDecay;
    std::atomic<float> _lastDamp;
    std::atomic<float> _lastMix;

    AUAudioUnitBus* _inputBus;
    AUAudioUnitBus* _outputBus;
    AUAudioUnitBusArray* _inputBusArray;
    AUAudioUnitBusArray* _outputBusArray;
    AUParameterTree* _parameterTree;

    // ADR-009 section 2 (decided part only): a render-thread-readable
    // snapshot of shouldBypassEffect, kept independent of `self` so the
    // render block never touches an Objective-C property getter (which
    // is not documented as render-thread-safe). Updated only by
    // -setShouldBypassEffect: below, off the render thread.
    std::atomic<bool> _bypassed;

    // ADR-008 section 1's Bridge Controller, online case: a strictly
    // serial dispatch queue, woken on a short fixed polling cadence
    // (plan-level decision, ADR-008's own "Remaining decisions" leaves
    // both the mechanism and the cadence to this plan). 1ms is chosen as
    // comfortably finer than the tightest realistic host block period
    // (128 samples @48kHz is about 2.67ms), keeping publication latency
    // within ADR-008 section 5's "one to a few block periods" estimate
    // without polling so fast it wastes CPU on a background queue.
    dispatch_queue_t _bridgeControllerQueue;
    dispatch_source_t _bridgeControllerTimer;
}
@end

@implementation AetherfieldAudioUnit

// ADR-010 lifecycle row: construction is off the render thread, at
// instantiation, not at allocateRenderResourcesAndReturnError:.
- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                      options:(AudioComponentInstantiationOptions)options
                                        error:(NSError **)outError {
    self = [super initWithComponentDescription:description options:options error:outError];
    if (self == nil) return nil;

    _path = std::make_unique<DiffusionStereoPath>();
    _bridge = std::make_unique<ParameterBridge>();
    _resetRequest = std::make_unique<ResetRequest>();
    _lastDecay = 0.5F; // matches DiffusionStereoPath/ParameterAutomation's own prepare()-time default
    _lastDamp = 0.0F;
    _lastMix = 1.0F;
    _bypassed = false;

    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000.0 channels:2];
    _inputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    // ADR-009 section 1 (decided): stereo-in/stereo-out.
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeInput
                                                              busses:@[_inputBus]];
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                              busType:AUAudioUnitBusTypeOutput
                                                               busses:@[_outputBus]];

    [self buildParameterTree];

    _bridgeControllerQueue = dispatch_queue_create("com.aetherfield.bridgecontroller", DISPATCH_QUEUE_SERIAL);
    _bridgeControllerTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _bridgeControllerQueue);
    dispatch_source_set_timer(_bridgeControllerTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               1 * NSEC_PER_MSEC, 0);
    __weak AetherfieldAudioUnit *weakSelf = self;
    dispatch_source_set_event_handler(_bridgeControllerTimer, ^{
        // ADR-008 section 1: the Bridge Controller drain-and-apply step,
        // online case. Never the render thread; this dispatch queue is
        // strictly serial, so it is never concurrent with itself.
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf != nil && strongSelf->_path != nullptr) {
            strongSelf->_bridge->drain(*strongSelf->_path);
        }
    });
    dispatch_activate(_bridgeControllerTimer);

    return self;
}

- (void)dealloc {
    if (_bridgeControllerTimer != nullptr) {
        dispatch_source_cancel(_bridgeControllerTimer);
    }
}

// ADR-009 section 1: the three normalized [0,1] AUParameters, addresses
// 0/1/2 matching aetherfield::wrapper::Parameter's own indices 1:1.
// implementorValueObserver is the UI producer role (ADR-008 section 1:
// "a UI or AUParameterObserver write off the render thread") -- every
// AUv3 host's own generic parameter view calls through here with no
// custom Aetherfield UI required.
- (void)buildParameterTree {
    AUParameter *decay = [AUParameterTree createParameterWithIdentifier:@"decay"
                                                                     name:@"Decay"
                                                                  address:(AUParameterAddress)Parameter::Decay
                                                                      min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                  unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    AUParameter *damp = [AUParameterTree createParameterWithIdentifier:@"damp"
                                                                    name:@"Damp"
                                                                 address:(AUParameterAddress)Parameter::Damp
                                                                     min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                 unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    AUParameter *mix = [AUParameterTree createParameterWithIdentifier:@"mix"
                                                                   name:@"Mix"
                                                                address:(AUParameterAddress)Parameter::Mix
                                                                    min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    decay.value = 0.5;
    damp.value = 0.0;
    mix.value = 1.0;

    _parameterTree = [AUParameterTree createTreeWithChildren:@[decay, damp, mix]];

    __weak AetherfieldAudioUnit *weakSelf = self;
    _parameterTree.implementorValueObserver = ^(AUParameter *parameter, AUValue value) {
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf == nil) return;
        const auto target = static_cast<Parameter>(parameter.address);
        strongSelf->_bridge->writeUi(target, value);
        switch (target) {
            case Parameter::Decay: strongSelf->_lastDecay.store(value, std::memory_order_relaxed); break;
            case Parameter::Damp: strongSelf->_lastDamp.store(value, std::memory_order_relaxed); break;
            case Parameter::Mix: strongSelf->_lastMix.store(value, std::memory_order_relaxed); break;
        }
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter *parameter) {
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf == nil) return 0.0F;
        switch (static_cast<Parameter>(parameter.address)) {
            case Parameter::Decay: return strongSelf->_lastDecay.load(std::memory_order_relaxed);
            case Parameter::Damp: return strongSelf->_lastDamp.load(std::memory_order_relaxed);
            case Parameter::Mix: return strongSelf->_lastMix.load(std::memory_order_relaxed);
        }
        return 0.0F;
    };
}

- (AUParameterTree *)parameterTree { return _parameterTree; }
- (AUAudioUnitBusArray *)inputBusses { return _inputBusArray; }
- (AUAudioUnitBusArray *)outputBusses { return _outputBusArray; }

// ADR-010 lifecycle row: allocateRenderResourcesAndReturnError: maps
// onto DiffusionStereoPath::prepare(). On failure, populate NSError and
// return NO; never substitute a different configuration silently
// (ADR-003 (d) rule 7's transactional guarantee, restated at the AU
// boundary by ADR-010).
- (BOOL)allocateRenderResourcesAndReturnError:(NSError **)outError {
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    // ADR-010 (b): {48kHz, 44.1kHz} only. Anything else must be rejected
    // here, not clamped.
    const double sampleRate = self.outputBusses[0].format.sampleRate;
    if (sampleRate != 48000.0 && sampleRate != 44100.0) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FormatNotSupported
                                         userInfo:@{NSLocalizedDescriptionKey: @"Aetherfield supports 48kHz and 44.1kHz only"}];
        }
        return NO;
    }

    DiffusionStereoConfig config {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0,   // overwritten immediately by the parameter tree's own Decay=0.5 default via ParameterAutomation, not a product value
        .t60PiSeconds = 4.0,
        .dMaxDb = 48.0,          // ADR-004/PT plan's test-fixture value; the product D_max remains a deferred Sonic-acceptance decision
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180339887498948482,
    };

    if (!_path->prepare(config)) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"DiffusionStereoPath::prepare() failed"}];
        }
        return NO;
    }
    return YES;
}

- (void)deallocateRenderResources {
    // Named ADR-010 gap, resolved here: the C++ instance is kept alive
    // across this cycle (not destroyed and reconstructed), so
    // nonFiniteCount() survives exactly as DS-B Task 3 requires, with no
    // need to relocate the counter into wrapper-owned state.
    [super deallocateRenderResources];
}

// ADR-008 section 7: host-triggered reset. Never touches _path directly
// -- only sets the wait-free flag. May be called from any thread.
- (void)reset {
    [super reset];
    _resetRequest->requestFromAnyThread();
}

// ADR-009 section 2 (decided part only): the host sets this off the
// render thread (Apple documents shouldBypassEffect as host-settable,
// not render-thread-settable). Updating the atomic snapshot here, rather
// than reading self.shouldBypassEffect from inside the render block, is
// what makes the render block's bypass check render-thread-safe.
- (void)setShouldBypassEffect:(BOOL)shouldBypassEffect {
    [super setShouldBypassEffect:shouldBypassEffect];
    _bypassed.store(shouldBypassEffect, std::memory_order_relaxed);
}

- (AUInternalRenderBlock)internalRenderBlock {
    // Captured by value into the block: raw pointers into state this
    // object owns for its own lifetime, matching Apple's own documented
    // pattern for internalRenderBlock (the block must not touch `self`
    // or any Objective-C object on the render thread).
    DiffusionStereoPath *path = _path.get();
    ParameterBridge *bridge = _bridge.get();
    ResetRequest *resetRequest = _resetRequest.get();
    std::atomic<bool> *bypassed = &_bypassed;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *actionFlags,
                               const AudioTimeStamp *timestamp,
                               AVAudioFrameCount frameCount,
                               NSInteger outputBusNumber,
                               AudioBufferList *outputData,
                               const AURenderEvent *realtimeEventListHead,
                               AURenderPullInputBlock pullInputBlock) {
        // ADR-008 section 7, first: drain any pending host-triggered
        // reset, unconditionally, before anything else -- including
        // before the frameCount == 0 check, since reset() itself takes
        // no frame count and there is no reason to delay a host's
        // explicit request past an empty callback.
        if (resetRequest->consumeIfPending()) {
            path->reset();
        }

        // ADR-008 section 3: scan the host event list exactly once,
        // collapsing to at most one HostParameterEvent per parameter,
        // then hand off to the portable, unit-tested coalescing logic.
        std::vector<HostParameterEvent> events;
        for (const AURenderEvent *event = realtimeEventListHead; event != nullptr; event = event->head.next) {
            if (event->head.eventType != AURenderEventParameter) continue;
            Parameter parameter;
            switch (event->parameter.parameterAddress) {
                case static_cast<AUParameterAddress>(Parameter::Decay): parameter = Parameter::Decay; break;
                case static_cast<AUParameterAddress>(Parameter::Damp): parameter = Parameter::Damp; break;
                case static_cast<AUParameterAddress>(Parameter::Mix): parameter = Parameter::Mix; break;
                default: continue;
            }
            // rampDurationSampleFrames is read (implicitly, by iterating
            // the event) and discarded -- ADR-008 section 4: "not
            // honored, not stored, not forwarded."
            events.push_back({parameter, event->parameter.value});
        }
        aetherfield::wrapper::applyHostEvents(*bridge, events.data(), events.size());

        // Offline/deterministic-adjacent note: this dispatch is always
        // the "online" path (a real internalRenderBlock invocation from
        // a host). The Bridge Controller's SEPARATE synchronous offline
        // drain (ADR-008 section 1's other half) applies only to this
        // project's own render tools calling DiffusionStereoPath
        // directly, not to anything reachable through this block.

        if (frameCount == 0) return noErr;

        AudioBufferList *inputData = nullptr;
        AudioUnitRenderActionFlags pullFlags = 0;
        const OSStatus pullStatus = pullInputBlock(&pullFlags, timestamp, frameCount, 0, &inputData);
        if (pullStatus != noErr || inputData == nullptr || inputData->mNumberBuffers < 2) {
            return pullStatus != noErr ? pullStatus : kAudioUnitErr_NoConnection;
        }

        const float *inputLeft = static_cast<const float *>(inputData->mBuffers[0].mData);
        const float *inputRight = static_cast<const float *>(inputData->mBuffers[1].mData);
        float *outputLeft = static_cast<float *>(outputData->mBuffers[0].mData);
        float *outputRight = static_cast<float *>(outputData->mBuffers[1].mData);

        // ADR-009 section 2 (decided part only): bit-exact dry
        // passthrough on bypass. The wet path is NOT skipped/paused here
        // -- this is the always-safe, unoptimized fallback ADR-009
        // itself names as the alternative to the still-unimplemented
        // T_silence-bounded hybrid mechanism (see Non-goals). Reading
        // the atomic snapshot, never self.shouldBypassEffect, keeps this
        // check render-thread-safe.
        if (bypassed->load(std::memory_order_relaxed)) {
            std::copy_n(inputLeft, frameCount, outputLeft);
            std::copy_n(inputRight, frameCount, outputRight);
            return noErr;
        }

        // ADR-009 section 1 (decided): sum-to-mono reduction feeding the
        // existing unmodified mono DiffusionStereoPath.
        static thread_local std::vector<float> monoScratch;
        monoScratch.resize(frameCount);
        for (AVAudioFrameCount i = 0; i < frameCount; ++i) {
            monoScratch[i] = 0.5F * (inputLeft[i] + inputRight[i]);
        }
        path->process(monoScratch.data(), outputLeft, outputRight, frameCount);

        return noErr;
    };
}

@end
```

### Step 4: `AetherfieldAudioUnitFactory.mm` — the component factory

```objc
#import <AudioToolbox/AudioToolbox.h>
#import "AetherfieldAudioUnit.h"

@interface AetherfieldAudioUnitFactory : NSObject <AUAudioUnitFactory>
@end

@implementation AetherfieldAudioUnitFactory

- (AUAudioUnit *)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                    error:(NSError **)error {
    return [[AetherfieldAudioUnit alloc] initWithComponentDescription:desc options:0 error:error];
}

@end
```

### Step 5: `Info.plist` — component registration

Register one Audio Unit component: type `aufx` (effect), subtype and
manufacturer codes as **placeholders** (e.g. subtype `Aeth`, manufacturer
`Josh`), matching the placeholder-identifier convention Step 1 already
names. `NSExtension` → `NSExtensionPointIdentifier` =
`com.apple.AudioUnit`, `NSExtensionPrincipalClass` =
`AetherfieldAudioUnitFactory`, `AudioComponents` array with one entry
naming the type/subtype/manufacturer above, `name` "Aetherfield: Reverb",
and `tags` = `["Effects"]`.

- [ ] **Step 6: Build**

Run: `xcodebuild -project platform/apple/Aetherfield.xcodeproj -scheme AetherfieldAUExtension -configuration Debug build`
Expected: `** BUILD SUCCEEDED **`. This is the entire verification this
plan performs for Task 3 — see "Honest verification status" below for
exactly what this does and does not prove.

- [ ] **Step 7: Run `scripts/check_dsp_source_drift.sh` (Task 1)**

Run: `./scripts/check_dsp_source_drift.sh`
Expected: `OK: DSP source lists match (6 files).`

- [ ] **Step 8: Commit**

```bash
git add src/auv3 platform/apple docs/decisions/ADR-010-device-lifecycle-matrix.md docs/decisions/ADR-012-host-device-acceptance-catalog.md
git commit -m "Implement AUv3 skeleton: AetherfieldAudioUnit, factory, Info.plist"
```

- [ ] **Step 9: Update `docs/start-here.md`**

Add a bullet, matching the existing style:

> - **AUv3 wrapper skeleton (`src/auv3/`, `platform/apple/`):** a minimal
>   `AUAudioUnit` implementing ADR-010's lifecycle mapping, ADR-008's
>   parameter bridge and §7 reset flag, and ADR-009's decided bus/dry-
>   passthrough behavior. **Compiles under Xcode only; not yet run under
>   `auval`, any host, or any device.** HT-1 through HT-12 remain unrun.
>   See `phase1-wrapper-skeleton-plan.md`.

### Honest verification status for Task 3 (read before claiming this done)

This plan's own Task 3 verification is deliberately narrow:

- **Proven:** the code compiles against the Apple SDK and links against
  `AudioToolbox`/`AVFoundation`; `cmake/DspSources.cmake` and the Xcode
  project's file list agree (Step 7).
- **Not proven, not claimed:** that the extension registers with the
  system, loads in any host, passes `auval`, produces correct audio,
  handles any HT-1…HT-12 case, or runs on any device or simulator. None
  of `auval`, a host, a simulator, or a device is exercised by this plan.
  Per this project's evidence discipline (testing.md's closing
  convention, restated verbatim by ADR-012): **a compile pass is not an
  HT result and must never be reported as one.**
- **The immediate next step**, separate from and after this plan, is
  running `auval -v aufx Aeth Josh` (ADR-012's tooling policy) and then
  HT-1 (cold instantiation/teardown) in an actual host — both explicitly
  out of scope here and not claimed as done by finishing this plan.

---

## Self-review checklist (per this project's writing-plans discipline)

- **Spec coverage:** ADR-008 §1 (single Bridge Controller) — Task 2 step
  4/5, Task 3 step 3. §2 (six mailboxes, coalescing, tie-break) — PB-1…
  PB-3. §3 (render-thread work bound) — PB-4/PB-5, Task 3 step 3. §4
  (timestamp handling) — Task 3 step 3 comment. §5 (offline determinism)
  — noted in Task 3 step 3; not separately tested here because it
  requires no new code beyond what Task 2 already tests (the offline
  path is literally "call `bridge.drain(path)` then `path.process(...)`,"
  already exercised by every PB-N case). §7 (reset flag) — PB-8, Task 3
  step 3. ADR-009 §1/§3 (decided parts) — Task 3 step 3. ADR-010's
  lifecycle table — Task 3 step 3, row by row in comments. ADR-010's
  source-list ownership — Task 1. ADR-012's re-verification instruction —
  Task 3 step 0.
- **Placeholder scan:** no "TBD"/"handle appropriately" language;
  bypass handling (`_bypassed`, `-setShouldBypassEffect:`, the render
  block's dry-copy branch) is complete, working code, not a sketch —
  an earlier draft of this plan left it as an unfinished placeholder and
  that was corrected before finalizing.
- **Type consistency:** `Parameter` (Decay=0, Damp=1, Mix=2) is used
  identically in `ParameterBridge.h`, the AUParameter addresses in Task 3
  step 3, and every PB-N test; `DiffusionStereoConfig`'s field names match
  the existing struct in `src/dsp/DiffusionStereoPath.h` exactly, copied
  from the same `validConfig()` fixture `tests/DiffusionStereoPathTests.cpp`
  already uses.

## What remains after this plan (explicitly not started)

- The ADR-009 hybrid bypass mechanism (`T_silence(decay)`-bounded CPU
  saving), and its dependency on ADR-011's `getAll()`.
- ADR-011's `getAll()`/`setAll()`, `fullState` persistence, and the
  StateRestore producer role.
- Any HT-1…HT-12 execution (ADR-012) — this plan makes them runnable for
  the first time; it does not run them.
- The container app's own minimal UI (it can be empty; AUv3 does not
  require the containing app to do anything beyond embed the extension).
- Real signing, bundle identifiers, and distribution.
