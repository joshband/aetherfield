# ADR-009 hybrid bypass mechanism — implementation task plan

**STATUS: Implementation merged at `145a69f` (implementation commit
`b2d6d65`); Task 4 evidence is recorded, with its documentation commit
pending.** This document began as a design/task plan. Its Tasks 1–3 now exist
in merged source; this reconciliation authorizes no subsequent implementation
work.

**Goal:** Replace the AUv3 skeleton's always-running bypass fallback with
ADR-009's hybrid policy: output bit-exact dry while bypassed, keep the wet
path alive on zero input only through a conservative whole-path silence bound,
then stop it after resetting it to exact zero.

**Architecture:** Keep bypass policy outside `src/dsp/`. A portable
`HybridBypassController` owns only render-thread state (`Running`/`Stopped`,
the elapsed counter, and an atomically published bound generation). The DSP
core exposes a control-thread-only read-back and calculation entry point so
the AU bridge-controller queue can publish a fresh whole-path bound whenever
an accepted Decay or Damp target changes. `AetherfieldAudioUnit` consumes that
publication and chooses live processing, zero-input draining, or no DSP call
for each callback.

**Tech stack:** C++20; existing CMake/CTest portable tests; Objective-C++ and
native `AUAudioUnit`; existing XcodeGen project generation.

**Decision records:**
[ADR-009](../decisions/ADR-009-production-bus-policy.md) §2, its “Design
note — hybrid bypass mechanism,” and its “Verification note”; [ADR-011's
read-back design note](../decisions/ADR-011-state-schema.md#design-note--read-back-accessor-and-setall-design-finalized-2026-09-19);
[ADR-008](../decisions/ADR-008-parameter-event-bridge.md) §1–§3 and §7;
[ADR-010](../decisions/ADR-010-device-lifecycle-matrix.md).

## Scope and non-goals

This is the follow-on to the merged wrapper skeleton plan at `63fa377`. It
does not alter the stereo bus layout, sum-to-mono route, parameter mailbox
roles, reset-request ownership, or DSP topology. It does not implement
ADR-011 state persistence or `setAll`, a host-visible “kill wet tail” control,
UI, `auval`, host/device acceptance HT-1…HT-12, signing, or deployment-target
policy changes.

The implementation must preserve these invariants:

- The render thread never calls `ParameterAutomation::getAll()`, performs
  logarithms/powers, allocates, blocks, or accesses Objective-C properties.
- AU bypass output remains a bit-exact copy of the two input channels. While
  bypassed, the wet path receives **zero** mono input and its output is
  discarded; live host input must never be injected into that tail.
- A bound publication caused by an accepted Decay **or Damp** change while
  bypassed and `Running` restarts the elapsed counter. ADR-009 §2 requires
  recomputation/bounding when either control changes; its design-note
  Decay-only wording is insufficient for the whole-path cascade allowance.
- At or beyond the bound, the render thread calls `DiffusionStereoPath::reset`
  exactly once before entering `Stopped`; a stopped path is therefore known
  zero, rather than merely assumed quiet.
- On un-bypass, `Running` immediately returns to live processing; `Stopped`
  also returns immediately because it was reset to exact zero.

## Bound definition and ownership

`silenceBoundSamples` is a saturating `std::size_t` count. It is computed
off the render thread from the actual prepared topology and current normalized
controls, then atomically published with a generation. It is never a measured
DS-10 constant.

The FDN term is ADR-009's verified, seconds-valued closed form:

```
T_silence(decay) = (m_max / m_min) * T60_zero(decay)
                   * log10(sqrt(N) / 1e-20) / 3
fdnSamples = ceil(T_silence(decay) * sampleRate)
```

The final whole-path value is the additive (not maximum) composition required
by ADR-009's verification note:

```
inputCascadeDrainSamples + fdnSamples + outputCascadeDrainSamples
```

`inputCascadeDrainSamples` follows the already-verified four-stage allpass
cessation derivation. `outputCascadeDrainSamples` is a conservative bound for
the larger of the two output cascades. The production helper must use closed
form geometric/logarithmic calculations and checked/saturating conversion to
samples; it must not port DS-10's bounded linear searches or its
200,000,000-iteration cap into the bridge-controller timer.

The helper's test matrix must establish conservatism for both declared sample
rates, Decay `{0, 0.5, 1}`, and Damp `{0, 1}`. If the current test derivation
cannot prove a Damp-independent output allowance at those endpoints, the
implementation must publish a conservative maximum over the two endpoints;
it must not silently treat the existing Damp=0-only numeric mirror as proof
for all Damp values.

## File map

| File | Responsibility |
|---|---|
| `src/dsp/ParameterAutomation.h/.cpp` | `NormalizedControls` read-back, restricted to the existing serialized control-thread discipline. |
| `src/dsp/DiffusionStereoPath.h/.cpp` | Control-thread forward that calculates the prepared path's whole-path bound without exposing its private `State` or Apple types. |
| `src/dsp/TailSilenceBound.h/.cpp` | Pure, allocation-free calculation over explicit prepared topology/control inputs; no render-thread caller. |
| `src/wrapper/HybridBypassController.h/.cpp` | Portable publication and render-state machine; no Apple types and no DSP ownership. |
| `tests/ParameterTransitionTests.cpp` | Read-back and bound-calculation regression cases. |
| `tests/HybridBypassControllerTests.cpp` | HB-1…HB-9 portable state-machine, generation, reset, and allocation tests. |
| `CMakeLists.txt`, `cmake/DspSources.cmake` | Build the new portable helper/controller and register the new CTest executable. |
| `src/auv3/AetherfieldAudioUnit.mm` | Bridge-controller publication and render-block integration; retains all existing lifecycle safety fixes. |
| `platform/apple/project.yml` and generated `platform/apple/Aetherfield.xcodeproj/` | Add the new portable source files to the extension target through the existing source-list convention. |
| `docs/testing.md`, `docs/start-here.md` | Record executed commands/evidence and update current status only after implementation verification succeeds. |

---

**Checklist interpretation after merge:** Tasks 1–3 are implemented in the
merged source, as recorded above. Their original step-level boxes are preserved
as the planning/execution history and are not retroactively checked without
separate evidence for each historical action. Task 4 below is the authoritative
whole-repository verification and documentation status.

## Task 1 — Control-thread read-back and whole-path bound

**Files:**

- Create: `src/dsp/TailSilenceBound.h`, `src/dsp/TailSilenceBound.cpp`
- Modify: `src/dsp/ParameterAutomation.h`, `src/dsp/ParameterAutomation.cpp`
- Modify: `src/dsp/DiffusionStereoPath.h`, `src/dsp/DiffusionStereoPath.cpp`
- Modify: `cmake/DspSources.cmake`, `tests/ParameterTransitionTests.cpp`,
  `tests/DiffusionStereoPathTests.cpp`

**Interfaces produced:**

```cpp
// ParameterAutomation.h
struct NormalizedControls { double decay; double damp; double mix; };
NormalizedControls getAll() const noexcept;

// DiffusionStereoPath.h; control thread only, never concurrent with setters.
ParameterAutomation::NormalizedControls controls() const noexcept;
std::size_t silenceBoundSamples() const noexcept;
```

`controls()` forwards the owned automation's `getAll()` result by value;
`silenceBoundSamples()` returns zero before preparation or if an invalid/
unrepresentable calculation would otherwise escape the documented bound; the
caller treats zero as “do not enter Stopped early” and publishes
`std::numeric_limits<std::size_t>::max()` instead. Thus a calculation failure
can only retain CPU work, never truncate a tail.

- [ ] **Step 1: Add failing read-back tests.** Assert the default triple is
  `{0.5, 0.0, 1.0}`; accepted finite setters return the stored, clamped values;
  rejected non-finite setters leave all three values unchanged. Exercise this
  only from the test's single control thread.

- [ ] **Step 2: Run the focused test and confirm the accessor is absent.**

  Run: `ctest --test-dir build/release -R aetherfield_dsp_param_tests --output-on-failure`

- [ ] **Step 3: Implement `NormalizedControls` and `getAll()`.** Return the
  existing `lastDecay_`, `lastDamp_`, and `lastMix_` plain doubles by value;
  add a comment stating the existing single-writer/control-thread-only rule.
  Do not add atomics or alter `publish()`, ramps, or `setAll`.

- [ ] **Step 4: Write failing bound tests before the helper.** At 48 kHz with
  the standard DS-B fixture, check the FDN-only closed-form component against
  ADR-009's recorded 4-second value (within one sample after `ceil`), verify
  the whole-path result exceeds that FDN term by the additive cascade
  allowances, and verify `Decay=1` exceeds `Decay=0.5`. Repeat the ordering
  check at 44.1 kHz. Test Damp endpoints and require the chosen output
  allowance to be no smaller than each independently evaluated endpoint.

- [ ] **Step 5: Implement `TailSilenceBound`.** Keep all topology values as
  explicit scalar/fixed-array inputs; use the stored float allpass coefficient
  widened to double; derive `T60_zero` from `t60Min`/`t60Max`; use `sqrt(N)`
  and `epsilon = 1e-20`; round upward and saturate every conversion/addition.
  Replace linear geometric drains with logarithmic ceilings. Return an invalid
  result rather than wrapping if a precondition, logarithm domain, or sample
  count is invalid.

- [ ] **Step 6: Add the two `DiffusionStereoPath` forwards.** `controls()`
  returns `automation.getAll()` and `silenceBoundSamples()` gathers the
  prepared FDN delays, allpass delays/coefficient, automation limits, and
  those controls within the core before delegating to the helper. Neither
  exposes `State`, FDN, automation, or a bypass flag in the public API.

- [ ] **Step 7: Run the two focused CTest targets.**

  Run: `ctest --test-dir build/release -R 'aetherfield_dsp_(param|diffusion_stereo)_tests' --output-on-failure`

- [ ] **Step 8: Commit the isolated DSP increment.**

  Run: `git add src/dsp cmake/DspSources.cmake tests/ParameterTransitionTests.cpp tests/DiffusionStereoPathTests.cpp && git commit -m "Add conservative tail-silence bound"`

## Task 2 — Portable hybrid-bypass state machine

**Files:**

- Create: `src/wrapper/HybridBypassController.h`,
  `src/wrapper/HybridBypassController.cpp`, `tests/HybridBypassControllerTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces produced:**

```cpp
enum class HybridBypassAction {
    ProcessLive, DrainWithZeros, DrainWithZerosThenReset, StayStopped
};

class HybridBypassController {
public:
    void publishSilenceBound(std::size_t samples) noexcept; // control thread
    HybridBypassAction beginBlock(bool bypassed, std::size_t frames) noexcept; // render thread
    void resetForHostReset(bool bypassed) noexcept; // render thread
};
```

`publishSilenceBound()` stores a saturating count then release-publishes a
generation. `beginBlock()` acquire-consumes at most one new generation. A new
generation only restarts the counter when bypassed and running. For a block
that crosses the threshold it returns `DrainWithZerosThenReset`: the caller
processes exactly that zero-input block, then resets the DSP before returning.
The render thread alone owns the state enum and elapsed counter, preventing
the bridge queue from racing `DiffusionStereoPath::process()` or `reset()`.

- [ ] **Step 1: Write HB-1 through HB-9 failing tests.** Cover: live mode
  before bypass; initial bypass enters zero-input drain; dry bypass produces
  no live-input action; exact-bound expiration returns
  `DrainWithZerosThenReset` once;
  later calls stay stopped; un-bypass from both states returns live mode;
  a new bound generation restarts a running tail; host reset while bypassed
  stays stopped; and 10,000 publications/decisions allocate zero times.

- [ ] **Step 2: Run the new executable and confirm it fails to build until
  the controller is added.**

  Run: `cmake --build build/release --target aetherfield_hybrid_bypass_tests && ctest --test-dir build/release -R aetherfield_hybrid_bypass_tests --output-on-failure`

- [ ] **Step 3: Implement the controller with no DSP or Apple dependency.**
  Use an atomic bound plus atomic generation, saturating elapsed addition, and
  a render-only `Running`/`Stopped` enum. A zero/invalid source bound must be
  converted to the saturated bound at publication, never treated as immediate
  silence.

- [ ] **Step 4: Add the CMake library/test wiring and run it.**

  Run: `cmake --build build/release --parallel && ctest --test-dir build/release -R aetherfield_hybrid_bypass_tests --output-on-failure`

- [ ] **Step 5: Commit the portable controller increment.**

  Run: `git add CMakeLists.txt src/wrapper/HybridBypassController.* tests/HybridBypassControllerTests.cpp && git commit -m "Add hybrid bypass controller"`

## Task 3 — AUv3 bridge and render integration

**Files:**

- Modify: `src/auv3/AetherfieldAudioUnit.mm`
- Modify: `platform/apple/project.yml`, generated
  `platform/apple/Aetherfield.xcodeproj/`

- [ ] **Step 1: Extend the existing bridge-controller timer handler.** Read
  `path->controls()` immediately before and after `ParameterBridge::drain`.
  Publish `path->silenceBoundSamples()` only when `before.decay != after.decay`
  or `before.damp != after.damp`. Publish the initial prepared bound
  immediately after every successful `prepare()`/resource allocation. Do not
  recompute per timer tick when controls are unchanged.

- [ ] **Step 2: Capture only raw C++ pointers into `internalRenderBlock`.**
  Add a `HybridBypassController` owned alongside `_path`, `_bridge`, and
  `_resetRequest`; capture its raw pointer exactly as the existing block does
  for those lifetime-stable objects. Retain the existing dealloc queue-drain
  ordering and the preallocated `_monoScratch` capacity discipline.

- [ ] **Step 3: Replace the unconditional bypass return with the controller
  action switch.** Copy both source channels exactly to output first whenever
  bypassed. For `DrainWithZeros`, fill only the already-allocated mono scratch
  range with `0.0F`, call `path->process`, and discard its output. For
  `DrainWithZerosThenReset`, do the same process call and then call
  `path->reset()` once before returning. For `StayStopped`, make no DSP call.
  For un-bypassed `ProcessLive`, retain the current sum-to-mono and normal
  processing loop unchanged.

- [ ] **Step 4: Route host reset through both existing and new render-owned
  state.** At the current `ResetRequest::consumeIfPending()` site, call
  `path->reset()` and `controller.resetForHostReset(bypassedSnapshot)`. This
  preserves ADR-008 §7's wait-free cross-thread request and keeps all actual
  DSP reset work on the render thread.

- [ ] **Step 5: Regenerate and build the extension target with warnings as
  errors if the project already enables them.**

  Run: `xcodegen generate --spec platform/apple/project.yml --project platform/apple/Aetherfield.xcodeproj`

  Run: `xcodebuild -project platform/apple/Aetherfield.xcodeproj -target AetherfieldExtension -configuration Debug build`

- [ ] **Step 6: Commit the AUv3 integration increment.**

  Run: `git add src/auv3/AetherfieldAudioUnit.mm platform/apple/project.yml platform/apple/Aetherfield.xcodeproj && git commit -m "Integrate bounded AUv3 bypass tail"`

## Task 4 — Whole-repository verification and evidence handoff

**Current Task 4 status:** Verification evidence is recorded in `testing.md`.
Steps 1–5 completed with controller-supplied commands/results; Step 6 remains
open because this documentation reconciliation is intentionally uncommitted.

**Files:**

- Modify: `docs/testing.md`, `docs/start-here.md`

- [x] **Step 1: Configure a clean Release tree (exit 0).**

  Recorded: `cmake -S . -B build/hybrid-bypass-reconcile -DCMAKE_BUILD_TYPE=Release`

- [x] **Step 2: Build and run every portable suite (exit 0; 8/8 passed).**

  Recorded: `cmake --build build/hybrid-bypass-reconcile --parallel && ctest --test-dir build/hybrid-bypass-reconcile --output-on-failure`

- [x] **Step 3: Run the DSP source-list drift check (exit 0; 7 files match).**

  Run: `scripts/check_dsp_source_drift.sh`

- [x] **Step 4: Build the tracked Xcode extension and record its exact
  target/configuration and warning status.** The unsigned `AetherfieldAUExtension`
  Release build exited 0; the ordinary signed build exited 65 because no
  development team is configured. This is compilation evidence only; do not
  label it host/device/auval validation.

  Recorded signed command (exit 65): `xcodebuild -project platform/apple/Aetherfield.xcodeproj -target AetherfieldAUExtension -configuration Release build`

  Recorded unsigned command (exit 0): `xcodebuild -project platform/apple/Aetherfield.xcodeproj -target AetherfieldAUExtension -configuration Release CODE_SIGNING_ALLOWED=NO build`

- [x] **Step 5: Update evidence and status accurately.** Record all commands,
  exit statuses, CTest count, the controller test identifiers, known limits,
  and that HT-1…HT-12 plus the kill-tail UX remain deferred. Change
  `docs/start-here.md` only after the above commands succeed; preserve the
  owner-authorization warning for any subsequent work.

- [ ] **Step 6: Commit documentation/evidence separately.**

  Run: `git add docs/testing.md docs/start-here.md && git commit -m "Document hybrid bypass verification"`

## Acceptance checklist

- [ ] `getAll()` is control-thread-only, returns the existing source-of-truth
  values, and does not introduce ADR-011 state restore or `setAll`.
- [ ] The published whole-path bound is conservative, saturating, and includes
  both diffusion cascades additively with the FDN term.
- [ ] No callback can feed live input into the wet path while AU bypass is on.
- [ ] A running tail receives zeros, then resets once and stops; neither the
  controller nor the render path allocates or blocks.
- [ ] Decay/Damp updates during bypass restart a running countdown; un-bypass
  resumes immediately from either controller state.
- [x] Portable CTest and unsigned Xcode build evidence is recorded. No claim is made
  for auval, a host, a device, or HT-1…HT-12.
