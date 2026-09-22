# Host/device acceptance — task plan

**STATUS: PLAN ONLY (2026-09-20). No host/device acceptance work has been
executed by this plan.** The owner authorized drafting this document after
hybrid-bypass merge `145a69f`. Writing or accepting it does not authorize
implementation, signing changes, installs, purchases, device time, or test
execution. Execution requires a separate, bounded owner authorization.

**Goal:** Establish reproducible AUv3 host/device evidence against the
implemented wrapper and hybrid-bypass baseline, while keeping every
unimplemented or inaccessible acceptance case visibly open.

**Architecture:** Exercise the native Apple AU boundary and real host behavior
on a named device/OS matrix. A future controlled AU test host supplies precise
inputs, event lists and failure injection; commercial host projects establish
actual host integration. Portable mechanism checks, simulator checks, macOS
structural checks and physical-device evidence remain separately labelled.

**Tech stack:** Existing C++20/CMake/CTest baseline; native Objective-C++
`AUAudioUnit`; Xcode/XcodeGen; development-signed iOS/iPadOS container and
extension; Apple profiling tools where supported. No new dependency is selected.

**Read first:** [start-here](../start-here.md), [roadmap](../roadmap.md),
[wrapper skeleton plan](phase1-wrapper-skeleton-plan.md),
[testing: AUv3 skeleton build](../testing.md#auv3-wrapper-skeleton-build-2026-09-20),
[ADR-007](../decisions/ADR-007-auv3-integration-comparison.md) remaining evidence,
[ADR-008](../decisions/ADR-008-parameter-event-bridge.md) §1–§5 and §7,
[ADR-009](../decisions/ADR-009-production-bus-policy.md) §2–§3 and the
**2026-09-20 live-state-envelope amendment**,
[ADR-010](../decisions/ADR-010-device-lifecycle-matrix.md) scoping/lifecycle,
and [ADR-012](../decisions/ADR-012-host-device-acceptance-catalog.md) HT-1…HT-12.

## Baseline and scope boundaries

The starting implementation is merge `145a69f` (hybrid implementation
`b2d6d65`), following wrapper skeleton/review baseline `63fa377`. Record the
actual full SHA at execution; any later source change creates a new evidence
baseline and requires impact-based reruns. This plan's authoring checkout had
pre-existing edits in `docs/start-here.md` and an untracked
`phase1-hybrid-bypass-plan.md`; neither is overwritten or treated as execution
authority here.

Current source, read during planning, includes `HybridBypassController`,
`TailSilenceBound`, the control-thread read-back, and AU hybrid glue. The
skeleton build record predates that merge and describes the older always-running
bypass fallback. Older plan/status/ADR prose saying the hybrid is unimplemented
does not override this source baseline. Conversely, source presence proves no
host/device behavior. This plan records **no new build, CTest, simulator,
`auval`, host, or physical-device result**; HT-1…HT-12 remain unrun.

The acceptance work is limited to existing stereo-in/stereo-out, sum-to-mono
processing, Decay/Damp/Mix transport, lifecycle/reset/fault handling, and hybrid
bypass. It does not authorize true-stereo DSP, topology changes, a new sample
rate, custom UI, modulation, Freeze/Bloom/Texture, presets/state implementation,
kill-tail control, product CPU promises, distribution, TestFlight/App Store
submission, CI, or a macOS port. Findings needing code changes become separately
scoped repair work; this plan is not permission to implement them.

### Known acceptance risks; not runtime findings

| Item | Source-level starting point | Consequence for execution |
|---|---|---|
| State recall | No `fullState` override or complete ADR-011 restore implementation | HT-7 is **blocked: implementation prerequisite absent**. Default Apple persistence cannot satisfy ADR-011 by implication. |
| Tail reporting | No `tailTime` override in `AetherfieldAudioUnit.mm` | HT-5 must measure the inherited property; the private bypass stop bound is not evidence of correct host tail reporting. |
| Offline automation | AU callback comments explicitly use the online asynchronous controller; deterministic synchronous drains belong to portable render tools | HT-10 with host automation is a real unverified case, not inherited portable determinism. |
| Input failure | Callback returns input-pull error without a specified silence/tail output policy | HT-4 policy remains an owner/design gate; report returned status and actual output without inventing acceptance semantics. |
| Bypass transitions/aliasing | AU dry-copy branch follows wet processing; no custom transition ramp is visible | HT-5 must cover real transitions and supported buffer aliasing, including possible input overwrite. No continuity or alias-safety pass is inferred. |
| Diagnostics | Required fault counters, bound generations and callback writes are not a public host diagnostic interface | A read-only test observation mechanism needs its own bounded design/authorization before claiming these assertions measured. |
| Packaging | `platform/apple/project.yml` targets iOS; container has no source files and placeholder identifiers | An unsigned compile proves neither installation nor host discovery; device-ready container/signing is a prerequisite, not an implicit task here. |

## Evidence contract and file map

Use these paths only in a separately authorized execution checkpoint:

| Path | Responsibility |
|---|---|
| `docs/phases/phase1-host-device-acceptance-plan.md` | Checkbox/status routing and remaining gates |
| `docs/testing.md` | Human-readable commands, findings, scope limits and links to evidence |
| `docs/agent-log.md` | Actual tool/model, checkpoint handoff, changed files, next action |
| `artifacts/host-device/<run-id>/manifest.json` | Run identity, complete matrix, configuration and per-case outcome |
| `artifacts/host-device/<run-id>/logs/` | Unedited build, install, host, profiler and case logs |
| `artifacts/host-device/<run-id>/audio/` | Input/reference/output PCM, hashes and transition excerpts |
| `artifacts/host-device/<run-id>/traces/` | Instruments traces and callback/event observations |
| `artifacts/host-device/<run-id>/projects/` | Exact saved host sessions and export settings where redistribution is permitted |

`<run-id>` is the UTC start timestamp followed by the tested short SHA. The
manifest records full SHA/dirty diff, compiler/SDK/Xcode versions, build
configuration and binary hash, device model/chip, OS **build**, host name and
version, audio route/interface, actual sample rate, requested and observed
block sequence, `maximumFramesToRender`, parameters, seed, trial count, timing
and allocation instruments, thermal/power state, and artifact paths/hashes.
Record exact commands and exit codes; GUI steps need ordered actions and
exported settings. Record unavailable observables as `unmeasured`.

Per case use `unrun`, `blocked` with a named gate, `pass`, `fail`, or
`measured-ungated`. A category is partial until all required subcases/axes have
results. `Not applicable` needs an explicit reason and cannot erase a required
matrix cell. A failed build/registration prerequisite blocks dependent runs;
it is not an HT pass. Retain failed trials and original PCM; do not clip,
silently normalize, replace references, or average away defects.

Byte/float-bit comparisons apply to identical binaries, fixed controls and
controlled event visibility. Report PCM identity separately from container
metadata identity. Never compare compressed audio as if it were lossless PCM.
Do not generalize across devices/toolchains. Any numerical tolerance needs its
metric, rationale and value recorded **before** observing the candidate result.

## Task 0 — Lock prerequisites and the run matrix

**Files:** execution manifest and this plan's handoff only. No product edits.

- [x] Obtain bounded execution authorization naming permitted builds, harness
  implementation if required, device installs, host operations and evidence
  collection. Purchases, accounts, signing credentials, OS upgrades/downgrades,
  distribution and unrelated host projects are separate owner actions.
  **Status (2026-09-21):** owner authorized full device matrix acceptance
  work: all four device/OS corners on physical iPhone and iPad (oldest A13/A12
  + newest available), Release configuration, signed builds via Automatic
  provisioning, AUM as primary host, physical device installs, and HT-1…HT-12
  execution (except state-restore blocking HT-7). Concrete device/host/OS
  identities and session scope will be recorded in the run manifest per the
  evidence contract.
- [x] Record `git status --short`, full HEAD SHA and dirty diff. Preserve
  user-owned work; use an isolated checkout for separately authorized test
  infrastructure or repairs. Confirm `145a69f` is an ancestor of the tested
  baseline and list any intervening functional changes.
  **Result (2026-09-20):** `git rev-parse HEAD` = `145a69fb2d0faa0407250c39c429bce7eae379b0`
  — HEAD **is** `145a69f` itself, not merely a descendant; `git log 145a69f..HEAD`
  is empty (no intervening commits). `git status --short` shows only the
  pre-existing docs-only dirty set: `M docs/start-here.md`, `M docs/testing.md`,
  and untracked `docs/phases/phase1-host-device-acceptance-plan.md`,
  `phase1-hybrid-bypass-plan.md`, `phase1-state-restore-plan.md`. No source,
  build, or test file is dirty.
- [x] Revalidate ADR-010's rolling **current released major minus one** policy
  separately for iOS and iPadOS using dated Apple release/support data. Record
  source URLs, release dates, current major, calculated floor, SDK availability
  and device compatibility. Do not reuse a beta version or an old ADR snapshot
  as the current released major.
  **Result (2026-09-20):** already current — ADR-010's own "Re-verification
  note (2026-09-20)" performed this exact check earlier today: no drift,
  iOS 27/iPadOS 27 remains current major, floor stays iOS/iPadOS 26+. Not
  re-run a second time same-day; re-check again only if this Task 0 pass
  is resumed on a later date.
- [x] Revalidate both newest available devices and oldest-corner OS support
  using Apple product/specification/support pages. ADR-012's recorded floor
  candidates are iPhone SE 2nd gen/A13 (or the documented iPhone 11-family
  alternative) and iPad 8th gen/A12. Its newest-device names are historical
  snapshots, not locked hardware in this plan. If the rolling floor excludes
  a recorded oldest corner, return that conflict to the owner; do not silently
  substitute a tier or widen OS support.
  **Result (2026-09-21):** ADR-010's rolling floor (iOS/iPadOS 26+) confirmed
  current on 2026-09-20, no change required. Device/tier matrix confirmed
  available to owner.
- [x] Create four concrete rows: oldest in-scope iPhone on oldest supported
  iOS, newest available iPhone on newest released iOS, oldest in-scope iPad on
  oldest supported iPadOS, newest available iPad Pro on newest released iPadOS.
  Record physical access and installed build for each. Missing or unavailable
  OS/device pairs remain blocked; simulator substitutions do not close them.
  **Status (2026-09-21):** execution authorization confirmed for full physical
  matrix. Concrete device/OS-build identities will be captured at device
  session time per the evidence contract.
- [x] Identify the weakest in-scope physical tier explicitly and include it
  for HT-11/HT-12 per policy (D). Do not extrapolate newest-device timing to
  that tier. If it already occupies a corner, reuse that identical recorded
  configuration rather than invent a fifth physical target.
  **Status (2026-09-21):** authorized; specific tier selection will be recorded
  at device session time.
- [x] Qualify two named host applications/versions that are actually available
  and licensed on the required families. Proposed candidates are **AUM** and
  **Cubasis 3**, subject to owner availability and live capability checks;
  these names are planning candidates, not verified compatibility. Record
  which supports rate changes, offline export, automation, bypass and
  transport-triggered reset. Unsupported host actions require a capable second
  host or controlled harness and a recorded coverage gap, not simulated proof
  of that commercial host's behavior.
  **Status (2026-09-21):** owner authorized **AUM as primary host** for the
  full device matrix. Per-capability support (rate changes, offline export,
  automation, bypass, transport-triggered reset) will be verified directly
  at device session time. No second host preauthorization required; coverage
  gaps will be recorded if a capability is unsupported.
- [x] Resolve development signing/team access, owner-approved test identifiers,
  provisioning, device trust/developer-mode access and installable container
  requirements. Inspect installed Xcode/SDK support for the locked matrix.
  **Status (2026-09-20): compile-signing sub-item closed; device install
  sub-item still open.** The owner confirmed Apple Developer Program
  enrollment already completed. A local-machine check found exactly one
  valid signing identity: `Apple Development: joshband@gmail.com
  (Q8RAXYZQK4)`, certificate SHA-1 `627E3654EFB5CD119AA064491B4E60A7F157C6D4`,
  Team ID `W2VVZU52J6` (individual account, `O=Joshua Band`). With the
  owner's explicit go-ahead, `platform/apple/project.yml` was edited to add
  `DEVELOPMENT_TEAM: W2VVZU52J6` and `CODE_SIGN_STYLE: Automatic` to both
  `AetherfieldAUExtension` and `AetherfieldHost`, `xcodegen generate` was
  re-run (2.46.0, matching the pinned version), and the previously
  exit-65 signed build command was re-run with `-allowProvisioningUpdates`.
  **Both targets now build and sign successfully** (exit 0,
  `codesign -dvv` confirms `TeamIdentifier=W2VVZU52J6` on both the
  `.appex` and the `.app`); full commands and output recorded in
  `testing.md`'s "Signed Release build unblocked (2026-09-20)" section.
  **Still unresolved:** `~/Library/MobileDevice/Provisioning Profiles/`
  remains absent locally even after this successful signed compile —
  whether that reflects "Automatic" signing not needing one for a
  device-less generic build, or a gap that will surface only once an
  actual device/App ID registration is attempted, is unverified. Device
  trust, developer-mode access on the physical matrix, and an actual
  install attempt are all still open and belong to Task 1, not this
  bullet's compile-time signing finding. `AetherfieldHost`'s build also
  emitted two unresolved warnings (missing interface-orientation/launch-
  storyboard declarations) — see `testing.md` for exact text; not
  confirmed to block install.
  **External reference found (not this project's own history):** a sibling
  repository, `~/Documents/Repos/latent-field` (a separate, JUCE-based
  product — architecturally distinct from Aetherfield's native-Apple-API
  AUv3 approach per ADR-007, so its JUCE-specific build steps do not carry
  over), has a working iOS-device AUv3 signed-build script
  (`scripts/build_ios_auv3_device.sh`) demonstrating a directly transferable
  `xcodebuild` flag pattern: a required `DEVELOPMENT_TEAM` value, default
  `CODE_SIGN_STYLE=Automatic`, and optional `CODE_SIGN_IDENTITY`/
  `PROVISIONING_PROFILE_SPECIFIER` overrides passed as `xcodebuild` build
  settings alongside `-destination 'generic/platform=iOS'`. It also has a
  manual `validation/HOST_AUDITION_SCORECARD.md` template (environment
  fields, a host×format matrix, a lifecycle-test checklist, CPU-by-instance
  table) — the same shape of artifact this plan's own evidence-contract
  table already specifies via `artifacts/host-device/<run-id>/`, so it is
  informative precedent, not a gap-filler to copy in directly. No other
  sibling repository under `~/Documents/Repos/` contains AUv3-specific
  source, project files, or device-signing scripts; `monument-reverb` and
  similarly named audio repos are macOS-only JUCE plugins (AU/VST3/
  Standalone) with no iOS/AUv3 target.
  Keep private signing data out of artifacts. Container/signing fixes require
  their own scope; this plan changes neither project nor identifiers.
- [ ] Approve a controlled native AU test-host/diagnostic design as a separate
  prerequisite if existing host APIs cannot expose counters, event timing,
  input failure injection or state transitions. It must exercise the actual
  AU callback and isolate any instrumentation from the shipping configuration;
  portable core calls alone cannot satisfy AU-boundary cases.

**Gate:** no device session is scheduled until exact hardware/OS/hosts and
authorized access exist. Discovery can complete with explicitly blocked rows;
matrix completion cannot. Record any Apple-data/ADR conflict here for owner
resolution; no ADR or roadmap edit is authorized by drafting this plan.

## Task 1 — Reproduce the baseline and establish structural reachability

**Files:** logs/manifest; later summaries in testing.md and agent-log.md.

- [ ] In the authorized checkout, configure, build and run the existing Release
  suites, recording the actual discovered count rather than copying a historical
  7/7 or 8/8 label:

  ```sh
  cmake -S . -B build/host-device-baseline -DCMAKE_BUILD_TYPE=Release
  cmake --build build/host-device-baseline --parallel
  ctest --test-dir build/host-device-baseline --output-on-failure
  bash scripts/check_dsp_source_drift.sh
  ```

- [ ] Reproduce the documented unsigned compile as a separate build-only
  baseline, retaining warnings; it authorizes no install:

  ```sh
  xcodebuild -project platform/apple/Aetherfield.xcodeproj \
    -scheme AetherfieldAUExtension -configuration Debug \
    -destination 'generic/platform=iOS' \
    CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY='' CODE_SIGNING_REQUIRED=NO \
    clean build
  ```

- [ ] Build/install the separately approved development-signed container on
  each named device, recording the concrete command or Xcode actions and
  identity of the embedded extension. Confirm each qualified host discovers,
  instantiates and renders the expected binary. A container needing code to
  launch or register the extension blocks this step pending bounded repair.
  **Status (2026-09-20): this exact named blocking condition has now
  occurred, found via Simulator rather than a physical device.** With the
  owner's go-ahead to move forward on non-device-dependent work while the
  physical-device leg is deferred, `AetherfieldHost` was built for the
  Simulator SDK and installed to a local simulator. Install failed:
  `AetherfieldHost.app is missing its bundle executable` — confirmed by
  direct inspection that `AetherfieldHost`'s `sources: []` target produces
  no Mach-O binary at all, on *both* the device and simulator builds (the
  earlier "builds and signs cleanly" result only meant `codesign` didn't
  error on a bundle with no executable, not that the bundle is installable
  — see `testing.md`'s "Correction note" and "iOS Simulator build and
  install attempt" for full detail). This blocked install on **any**
  target, physical or simulated.
  **Repair applied and verified (2026-09-20), owner-authorized.** Added
  `platform/apple/AetherfieldHost/main.m` — a bare, behavior-free
  `UIApplicationDelegate` run without a scene manifest — and changed
  `AetherfieldHost.sources` from `[]` to `[{path: AetherfieldHost}]` in
  `project.yml`. No UI, scene delegate, or other content was added.
  Rebuilt clean for both simulator and device: both now link a real
  Mach-O (`codesign -dvv` confirms `Format=app bundle with Mach-O ...`,
  previously `Format=bundle` with no executable). **Simulator reinstall
  now succeeds** (`xcrun simctl install` exit 0;
  `com.aetherfield.placeholder.AetherfieldHost` confirmed present via
  `simctl listapps`), and the embedded extension is independently visible
  to the OS plugin registry (`pluginkit -m` reports
  `...AetherfieldHost.AetherfieldAUExtension(1.0)`). This is registration
  evidence, not HT-1's "discovers, instantiates and renders" — no host or
  harness has queried `AVAudioUnitComponentManager` or instantiated the
  `AUAudioUnit`. Full commands and output in `testing.md`'s "Bounded
  repair applied and verified" section. **Still open:** the CFBundleVersion
  mismatch warning persists on both builds; the orientation/launch-
  storyboard warnings persist on the device build (not observed on this
  simulator run); none investigated further. Physical-device install
  remains deferred and unattempted; only the container's installability
  is now demonstrated, on simulator.
  **Closer evidence added (2026-09-20), owner-requested:** a minimal
  `AetherfieldHarnessTests` XCTest target (hosted in `AetherfieldHost` via
  `TEST_HOST`) now actually queries `AVAudioUnitComponentManager` and
  instantiates the `AUAudioUnit` out-of-process
  (`kAudioComponentInstantiation_LoadOutOfProcess`), on the same
  simulator. **Passed**: discovered `name=Reverb manufacturerName=Aetherfield
  version=1`; instantiated as `class=AUAudioUnit_XH` (Apple's real
  out-of-process XPC proxy class, confirming the actual extension-hosting
  path, not an in-process stand-in) with matching `componentName`/
  `manufacturerName`. Full log and exact commands in `testing.md`'s
  "AVAudioUnitComponentManager/AUAudioUnit instantiation harness" section.
  **Still not HT-1:** no `allocateRenderResourcesAndReturnError:` call,
  no render, no repetition, no physical device, single instance only.
  **Render attempted (2026-09-20), owner-requested, and it failed:** a
  second harness test negotiates a 48 kHz/stereo format, calls
  `allocateRenderResourcesAndReturnError:` (succeeds), then calls
  `renderBlock` once with 512 silent frames. The render call itself
  **fails** with `kAudioUnitErr_RenderTimeout` (OSStatus `-66745`).
  Working theory (unconfirmed): calling `renderBlock` directly from an
  XCTest thread is likely not a valid real-time render context for an
  out-of-process AUv3; a real render harness probably needs an
  `AVAudioEngine` or a real-time-priority driver. This is a genuine open
  problem, not closer HT-3 evidence — no successful render exists yet on
  any target. Full log in `testing.md`'s "(a) Render extension" section.
  **Follow-up AVAudioEngine spike (2026-09-20), owner-approved:** a third,
  separately named Simulator-only XCTest test instantiates the extension as
  an out-of-process `AVAudioUnit`, puts it in an `AVAudioEngine` offline/manual
  graph, and renders one silent 48 kHz stereo 512-frame buffer. The targeted
  command exited 0; manual mode enabled, engine start, and offline render all
  returned success, with `renderedFrames=512`. This avoids the direct-call
  timeout but is **not a successful AU render-path finding**: Simulator logging
  also reported the plug-in connection interrupted/invalidated while in use,
  and this test has neither callback instrumentation nor a sample-value oracle
  proving `internalRenderBlock` processed the buffer. It therefore supports
  the engine-graph direction without confirming the timeout root cause or
  advancing HT-1/HT-3. Keep the raw-direct failure intact and require the
  separately scoped controlled-host/diagnostic design before claiming an
  extension render callback. See `testing.md`'s "(c) AVAudioEngine
  offline-render experiment" for the exact command and output.
  **AVAudioEngine output oracle refinement (same day):** the status-only
  one-block test was replaced with an eight-block/4,096-frame test using a
  one-sample impulse. Since default Mix is wet-only and the configured FDN
  minimum delay is 27 ms, its assertion requires a non-zero late output sample
  and cannot pass on source passthrough alone. It **failed**: the engine
  reported all 4,096 frames rendered, but `latePeak=0`, with the same plug-in
  interruption log. This is now a reproducible end-to-end engine-render
  failure, not merely an unconfirmed callback-observability gap. It does not
  locate the failure inside the extension versus its XPC/engine lifecycle, so
  no repair is authorized or inferred. Keep this red diagnostic intact; the
  next investigation is lifecycle/XPC boundary evidence, not HT-1/HT-3
  expansion.
  **Crash-report confirmation (2026-09-21):** a fresh reproduction produced
  `AetherfieldAUExtension-2026-09-21-114143.ips`, which records an
  `EXC_BAD_ACCESS`/SIGSEGV at address zero on `AUOOPRenderingServer-*`. The
  symbolicated top frame is this project's
  `AetherfieldAudioUnit.mm:514`, the dereference of the closure-captured
  `inputBufferList`. This confirms that the engine path is crashing the
  extension because `internalRenderBlock` snapshots allocation-owned
  `_inputPCMBuffer`/`_monoScratch` pointers before the host's later allocation
  step; it is not merely a disconnected graph or a DSP silence result. The
  raw direct-render timeout remains a separate host-context finding. Any repair
  must make this lifecycle safe and needs a new bounded implementation
  authorization; none is implied by diagnosis. See `testing.md` "(c)".
  **Lifecycle repair and verification (2026-09-21):** owner-approved bounded
  implementation replaced the pre-allocation raw-pointer capture with
  AU-lifetime atomic resource slots loaded by the cached render block per
  callback. Allocation publishes fully formed input/mono scratch resources;
  deallocation withdraws them before release; absent resources return
  `kAudioUnitErr_Uninitialized`. The existing 4,096-frame engine impulse oracle
  changed from `latePeak=0` plus XPC interruption to a passing
  `latePeak=0.014125`, and the previously red direct 512-frame render now also
  returns `status=0`. This resolves the reproduced Simulator crash/timeout, but
  remains Simulator-only and does not close HT-1 or HT-3.
  **Ragged-block oracle extension (2026-09-21):** the impulse oracle now
  repeatedly requests `{1,13,64,512,3}` (34 requests to its 4,096-frame cap)
  at both 48 kHz and 44.1 kHz, while preserving its delayed-wet-output
  assertion. Focused Simulator XCTest runs exited 0 with `latePeak=0.014125`
  at 48 kHz and `0.014429` at 44.1 kHz. This is varying callback-size evidence
  on Simulator only; it is not HT-1/HT-3 or physical-device acceptance.
  **Cached-block reallocation regression (same day):** the direct harness now
  intentionally fetches `renderBlock` before allocation, retains it through a
  512-frame render, deallocates/reallocates, and renders again through that
  unchanged closure. Both calls returned `status=0` and reallocation succeeded.
  This protects the repaired lifecycle contract across reconfiguration, but is
  still one Simulator instance/two blocks at one format, not HT-1/HT-3.
  Also same day: the CFBundleVersion/orientation/launch-storyboard
  warnings named above are now resolved and re-verified by rebuild on
  both device and simulator (0 warnings), with install/`pluginkit`
  re-confirmed unaffected — see `testing.md`'s "(b) Three build warnings"
  section for the exact fix and the discarded `UIRequiresFullScreen`
  attempt (deprecated as of iOS 26.0, this project's own floor).
  **Host UIScene repair and first physical-device result (2026-09-21):** the
  empty host's focused Simulator XCTest had emitted UIKit's legacy
  no-scene-lifecycle warning. With owner approval, a single generated
  `UIApplicationSceneManifest` and an otherwise empty scene-delegate
  `UIWindow` property replaced that lifecycle; no storyboard or app UI was
  added. The same focused harness then passed on Simulator with no runtime
  warnings. On the subsequently connected iPhone 16 Pro Max (iOS 27.0,
  build 24A437), the signed host/test bundle built and launched, but its
  discovery assertion **failed**: `AVAudioUnitComponentManager` returned zero
  Aetherfield matches. This is a failed physical-device discovery prerequisite,
  not HT-1/HT-3 evidence; no diagnosis or repair is authorized by it. Exact
  command and `.xcresult` outcome are recorded in `testing.md`'s "Host UIScene
  lifecycle repair" section.
  **Follow-up review:** a 15-second registration-notification/poll probe still
  found no matching component, so this is not established as an immediate-query
  race. A failed, reverted metadata-convention experiment did not change that
  result. After reconnection, the missing-host-`CFBundleDisplayName` candidate
  was installed and tested too; discovery still returned zero matches and the
  device logged `IPCAUClient: can't connect to server (-66748)`, which the
  iPhoneOS SDK names `kAudioComponentErr_NotPermitted`. It was reverted. This
  is evidence of a permission/service-connection failure, not a root cause or
  an authorization for another speculative repair.
- [ ] **Permission/service root-cause repair:** the AU registration was missing
  `sandboxSafe`, and the host app was missing the `inter-app-audio` entitlement.
  The installed SDK headers and Xcode Audio Unit host template establish these
  as security declarations rather than display-name guesses. A regression
  assertion now requires `kAudioComponentFlag_SandboxSafe`, and `sandboxSafe:
  true` is present in the source-of-truth registration. The first signed
  device rerun after that change no longer logged `-66748`, but still found no
  component and logged the host display-name warning. Adding the host's
  `inter-app-audio` entitlement is the next one-variable repair, but the
  current managed provisioning profile rejects it because the App ID/profile
  does not include the Inter-App Audio capability. Refresh that capability and
  profile before rerunning the focused physical-device XCTest. Until then,
  device discovery remains failed/unverified; no HT-1/HT-3 pass is claimed.
- [x] **Focused physical-device discovery/instantiation retest:** after
  `-allowProvisioningUpdates` refreshed the managed profile to include
  Inter-App Audio, the focused XCTest passed on the iPhone 16 Pro Max
  (iOS 27.0, build 24A437): one test, zero failures; the component was
  discovered and instantiated as `AUAudioUnit_XH`. The prior `-66748`
  NotPermitted failure did not recur. This closes only the focused discovery /
  instantiation prerequisite, not HT-1/HT-3. The host still logs
  `bundle display name is nil`; retain that warning as a separate cleanup
  item unless separately authorized.
- [ ] Determine `auval` applicability before invoking it. The current project
  supplies an **iOS-only artifact**; macOS `auval` cannot be assumed to load it.
  Record `blocked: compatible macOS validation artifact absent` for this
  optional structural leg unless one is separately authorized and built.
  Do not add a macOS target to satisfy a tooling suggestion. If such an
  artifact exists, run `auval -v aufx Aeth Josh` only after checking its actual
  component IDs, retain tool/version/full output, and treat structural failure
  as blocking for that artifact. `auval` closes no HT category.

**Gate:** portable/build successes remain baseline evidence only. iOS
discovery/instantiation is measured explicitly; macOS `auval` unavailability
does not masquerade as either an iOS failure or an iOS pass.

**Bounded HT-1/HT-3 physical evidence (2026-09-21):** a separate
`platform/apple/AetherfieldHarnessTests/PhysicalAcceptanceTests.mm` harness
was added after the Inter-App Audio profile refresh. Its first HT-3 attempt
exposed and corrected a harness partition-loop defect before any product
conclusion was drawn. The corrected focused device run passed 6 tests with
zero failures: the four existing focused tests, a bounded HT-1 smoke with 20
same-instance cycles and 20 fresh instantiate/render/destroy cycles at each
of 44.1 kHz and 48 kHz, and HT-3 float-bit-exact one-shot versus partitioned
rendering over 131,072 frames at both rates, including all three required
partition sequences and zero-frame handling. Result bundle:
`/tmp/aetherfield-physical-acceptance-final/Logs/Test/Test-AetherfieldHarness-2026.09.21_13-44-41--0400.xcresult`.

This is bounded evidence, not closure: the full HT-1 100-cycle contract,
one-second cycles, controlled-fault persistence, resource-growth method, and
HT-3's 4096/observed-host-maximum coverage, raw-PCM retention, and explicit
capacity-rejection probe remain open.

**Expanded HT-1 result and HT-3 diagnostic (2026-09-21):** the same physical
harness was extended to the plan's 100 same-instance one-second cycles and
100 fresh instantiate/render/destroy cycles per rate. HT-1's expanded test
passed on the connected iPhone 16 Pro Max (iOS 27.0, build 24A437). Adding the
required `{4096}` HT-3 partition produced a reproducible bit-exact failure at
sample 4096 on both 44.1 and 48 kHz; the prior fixed, ragged and zero-frame
sequences were retained. A later source-level review found an underallocated
two-buffer `AudioBufferList` in the harness helper, so that result is no
longer admissible as an AU-boundary finding. The helper was corrected and its
simulator-SDK build passed; a device/simulator rerun was initially blocked by
CoreDevice/CoreSimulator destination loss, which subsequently cleared without
a reboot. The corrected harness reran on the physical iPhone 16 Pro Max:
`{4096}` and two other partition sets are now bit-exact at both rates
(confirming the prior `{4096}` failure was the harness defect), but a new,
reproduced mismatch appears on the fourth partition set
`{0,1,13,64,512,977,1024,3,0}`, diverging mid-stream rather than at the
boundary — see `testing.md`'s "Corrected-harness physical rerun, no reboot"
section. This is an open HT-3 finding requiring its own bounded
investigation; no production repair or HT-3 closure is inferred. The
over-capacity rejection probe remains unrun because the current callback
does not explicitly reject a frame count above its allocated scratch
capacity.

## Task 2 — Lifecycle, buffers and recovery: HT-1, HT-2, HT-3, HT-4, HT-6

**Matrix:** both rates `{44100, 48000}` except HT-4 may use 48000; nominal
128 frames and stress 32 where the host permits. Run corner smoke coverage
and the controlled cases once per family/OS corner; multiply by a second host
for HT-2 and HT-4, and use a host proven to issue reset for HT-6. Record actual
host-selected sizes when controls are unavailable.

- [ ] **HT-1:** 100 allocate/render/deallocate/reallocate cycles on one AU and
  100 instantiate/render/destroy cycles per rate. Render one second per cycle
  with fixed controls `(Decay=.5, Damp=.5, Mix=.5)`. Record each allocation
  result/error and allocation/liveness or resident-memory method after warmup.
  Seed a controlled fault before a same-AU deallocate/reallocate cycle and
  print counter before/after; it must persist. A genuinely new AU has its own
  fresh counter. Gate on no failed supported allocation, crash, use-after-free,
  retained per-cycle resource growth or lost same-instance counter. RSS alone
  cannot prove leak freedom; unexplained growth needs investigation.
- [ ] **HT-2:** 20 alternating 44100→48000→44100 cycles. At a valid state,
  request 96000 Hz, mono output, mismatched bus rates and unsupported channel
  layouts; record request, returned BOOL and NSError domain/code/text at the
  actual rejection boundary. Prove prior-state preservation by comparing the
  next output with an identically advanced control instance that did not
  receive the rejected request. Do not reset both paths and thereby erase
  evidence of damaged prior state. Gate on rejection without fallback and
  bit-identical continuation where the same-binary contract applies.
- [ ] **HT-3:** at fixed settled controls, compare a one-shot whole-input
  reference on an instance explicitly allocated for that capacity with fixed
  partitions `{1,13,64,512,3}`, ragged `{7,29,3,211,5}`, and
  `{0,1,13,64,512,977,1024,3,0}`. Exercise 4096 and each observed host maximum
  with buffers/preparation sized for that maximum; adapt the final remainder
  explicitly. Use 65536 input frames followed by 65536 zero frames, with a
  fixed seeded stereo input retained as raw PCM. Gate on float-bit identity
  and no out-of-range access. Record largest exercised frame count and every
  observed host maximum. Requests exceeding declared capacity are a separate
  rejection probe in a controlled, instrumented harness, not ordinary valid
  audio calls.
- [ ] **HT-4:** perform 20 injected pull errors and 20 successful pulls with
  deliberately shortened valid buffer extents, at nominal and small blocks.
  Poison unused storage in the harness to detect unintended reads; retain
  output/status/counters and memory diagnostics. Also record natural host
  underruns if observed. Do not deliberately overload unrelated sessions or
  speakers. Gate on no crash, invalid access, non-finite output, allocation or
  blocking; output-policy acceptance remains blocked until the owner/design
  record specifies behavior on missing input. Short-buffer malformed-input
  evidence is distinct from a real host underrun.
- [ ] **HT-6 explicit host reset:** inject a known fault, request `-reset`,
  then issue zero frames and a nonempty zero-input block. ADR-008 §7 requires
  the **host request to be consumed even by the zero-frame callback**. The
  nonempty output starts from exact silence; the cumulative count persists.
  Repeat while hybrid bypass is Running and Stopped and while transport stops.
- [ ] **HT-6 self-recovery:** inject NaN/Inf through the AU input, then zero
  frames without a host reset, then a nonempty zero-input block. The zero-frame
  call must **not consume the core's pending self-recovery**; recovery occurs
  before the next real sample and preserves cumulative counts. Record faulting
  block output, flags and counters rather than requiring that block to be
  silently repaired mid-sample. Keep fault injection off live monitoring.
- [ ] **HT-6 concurrency:** 10000 reset requests from a second thread while
  callbacks run with `{0,32,128,512}` frames. Record available sanitizer/race
  instrumentation and callback ownership observations; run simulator-only
  sanitizer support as supplemental evidence if physical-device support is
  unavailable. Gate known violations; a clean finite run establishes only
  “no race detected in this configuration,” not universal thread safety.

**Checkpoint:** publish separate pass/fail/blocked subcases. Defects stop the
affected leg and create an owner-reviewable repair scope; do not patch source
as part of acceptance execution without that authorization.

## Task 3 — Hybrid bypass and automation: HT-5 and HT-8

**Matrix:** both qualified hosts; both rates for HT-5; 48000 for HT-8;
nominal 128 and stress 32 frames where supported. Fixed input files and event
scripts are saved, with host-native bypass behavior distinguished from writes
to `shouldBypassEffect`.

- [ ] **HT-5 dry/alias:** use asymmetric L/R PCM (including one silent channel
  and opposite-polarity samples). Check bit-exact L→L and R→R dry passthrough
  while bypassed, with distinct buffers and every layout the actual AU reports
  as supported, including corresponding-channel in-place buffers if advertised.
  Do not infer dry correctness from the mono Mix=0 core behavior. Record
  `canProcessInPlace` and whether the host pulls audio while bypassed.
- [ ] **HT-5 live history:** precondition with a unit impulse, 10 seconds of
  seeded noise, and 10 seconds of bounded constant input. Cover normalized
  Decay `{0,.5,1}`, Damp `{0,1}`, Mix `{0,.5,1}` at both rates. Record the
  latched input envelope, published bound/generation, zero-input drain count,
  and actual state transitions through approved diagnostics. The timer must
  use ADR-009's live-envelope amendment, not the superseded `sqrt(N)` impulse
  assumption or DS-10's measured silence constant.
- [ ] **HT-5 transitions:** un-bypass before the conservative bound, at the
  bound-crossing callback and after Stopped. Compare the hidden tail against an
  identically advanced zero-input reference where observable. Gate on zero
  injection during bypass, no early tail reset, exactly one reset on expiration,
  no DSP processing after Stopped, and correct restart from known silence.
  Record maximum adjacent-sample delta and PCM excerpts across every toggle.
  The ADR forbids a hard output-source discontinuity; do not reuse PT-7's
  0.354 observation as a threshold. A known abrupt dry/wet source switch is
  a contract finding requiring separate design/repair, even when tail state
  itself is preserved. Listening informs the owner; it does not erase it.
- [ ] **HT-5 publication stress:** update Decay and Damp separately during a
  drain, rapidly toggle bypass twice, delay a controller turn in the controlled
  harness, and reset while Running/Stopped. Confirm no stale finite bound can
  stop a later high-energy tail early; each required fresh publication restarts
  elapsed drain time. Exercise a rejected/non-finite bound calculation through
  an approved test seam: its safe behavior is continued draining, not early
  silence. A timeout records an incomplete long-tail case, never a pass.
- [ ] **HT-5 tail property:** record the actual AU `tailTime` and measured
  time to exact silence for each Decay/Damp/input case, and whether changes
  refresh the property. Flag reported time shorter than observed tail as a
  defect; distinguish host tail metadata from internal stop bounds. The absent
  override means this leg may require separately authorized implementation.
- [ ] **HT-8 event density:** supply batches of `{0,1,3,64,1024,10000}` valid
  parameter events per callback, repeated 100 times, cycling addresses with
  repeated same-parameter writes. Include unknown addresses, different sample
  offsets, and both step and ramp event forms supported by the API. Record
  delivered/recognized counts and final targets. Gate on the accepted
  last-write-wins semantics and at most three Host mailbox writes per callback,
  no allocation/lock; do not call the entire list traversal constant-time.
- [ ] **HT-8 ordering/latency:** overlap Host and generic-parameter-view UI
  writes using distinguishable values; verify Host-then-UI drain precedence
  only where those producer roles can actually be observed. StateRestore is
  absent and excluded. Record delivery→publication→first affected sample
  latency and maximum events actually delivered by each host. Current 1ms
  controller scheduling is a configured interval, not a proven latency bound.
  Retain dense-automation renders for owner audition of coalescing; host ramp
  duration/sample accuracy is not a supported fidelity promise.

## Task 4 — Instances and offline behavior: HT-9 and HT-10

- [ ] **HT-9 isolation:** at 48000/128, run two simultaneous instances with
  different retained stereo input and fixed settled triples `(0,.25,1)` and
  `(1,.75,.5)`. Compare each output bit-for-bit with its single-instance
  control run. Reset, bypass and automate one instance while observing the
  other's unchanged controls/output. Gate isolation; record timing separately.
- [ ] **HT-9 capacity:** on each named physical tier, measure 1, 2, 4 and 8
  instances for five minutes per count, stopping at reproducible dropout,
  critical thermal state or the predeclared ceiling of 8. Record thermal/power
  state, exact host counters, audio evidence and other workload. A clean run
  at 8 establishes only “at least 8 under this setup,” not maximum capacity
  or a product instance-count target. No count threshold is invented.
- [ ] **HT-10:** save an exact project in each host supporting non-realtime
  export and export it three times at each rate, first with fixed controls,
  then with a retained automation script. Disable dither/normalization and use
  lossless float PCM where available; record pre-roll, tail/export duration,
  event visibility and observed blocks. Compare audio payload bits and full
  file hashes separately. Gate same-configuration PCM identity; report metadata
  differences separately. Asynchronous publication is a possible finding,
  not grounds to loosen a failing repeatability gate after seeing results.
- [ ] Record real-time export separately when a host lacks offline export;
  it cannot satisfy HT-10. Any comparison to portable renders is supplemental
  and requires matched downmix, controls, event visibility, pre-roll and tail;
  otherwise mark the comparison unmeasured.

## Task 5 — Actual callback audit and timing: HT-11 and HT-12

**Prerequisite:** a separately approved observation method proven usable in
the extension process on the named physical device. Use Instruments allocation
and thread-state traces with callback attribution where supported, plus a
transitive call-graph audit. If callback boundaries or lock events cannot be
attributed, the corresponding gate is unmeasured. Any needed instrumentation
code requires a bounded diagnostic implementation plan; debug counters must
not silently enter the shipping build.

- [ ] **HT-11:** for both rates, trace first callback after allocation, warmed
  steady-state, `{0,32,128,512}` blocks, dense automation, host reset,
  self-recovery, bypass draining/expiration/Stopped and reactivation. Observe
  60 seconds per reachable steady scenario plus 100 occurrences of each
  transition. Audit allocations and locks **separately** on the actual render
  thread; zero attributable calls is the gate within instrument coverage.
  Include memory first-touch/page-fault observations and instrumentation
  blind spots. Host CTest allocation overrides or static review alone cannot
  establish an on-device zero result.
- [ ] **HT-12:** use an optimized development-signed build with symbols. On
  every physical corner including the weakest tier, measure both rates at
  available `{32,64,128,256,512}` block sizes for ten minutes per configuration
  after a two-minute warmup; absent sizes remain unmeasured. Cover live dense
  input with high Decay, dense automation, hybrid Running and Stopped, and
  reset transitions; retain state-labelled timing distributions.
- [ ] Begin runs unplugged, low-power mode off, thermal state nominal where
  achievable, and record battery, ambient conditions and other active work.
  Record thermal state throughout; flag changes and segment results. Stop at
  critical thermal state. Do not silently cool, shorten or omit bad intervals.
  Repeat a representative 64-frame case plugged in as a labelled power-mode
  observation, not merged into the unplugged distribution.
- [ ] Report callback count, minimum/median/p95/p99/p99.9/maximum execution
  time, timing resolution/overhead, deadline `frameCount / sampleRate`, maximum
  duration/deadline ratio and every host-reported underrun/dropout. Time the
  full AU callback, distinguishing pull-block work from plugin-owned work
  where observable. Paired instrumentation-on/off runs estimate observer cost;
  do not subtract an assumed cost from worst cases.
- [ ] Keep CPU/headroom and multi-instance capacity **measured-ungated** until
  the owner establishes product budgets. Reproducible plugin-attributable
  underruns are functional failures independent of that missing numeric
  budget; distinguish attribution from coincidence with other host work.

## Task 6 — Deferred state gate, review and handoff

- [ ] Keep **HT-7 blocked** until a separately authorized ADR-011 state
  implementation is merged. Read ADR-011 and design its exact version,
  validation, tuple atomicity and host-thread cases at that time; this plan
  does not reopen or implement that scope. Later evidence must cover
  before/after-allocation restore, same/cross-rate round trips, malformed and
  foreign state, unsupported versions/fixture mismatch, and one atomic triple
  publication with Host→StateRestore→UI precedence. Generic host parameter
  persistence is not an alternate pass.
- [ ] Have Luna independently reconcile commands/artifacts and coverage, Sol
  review numerical/tail/concurrency findings, and Astra reconcile scope and
  owner gates. Record actual model/tool and review limitations, not inferred
  roles. This is planned execution staffing; no agents or implementation are
  authorized merely by listing it here.
- [ ] Publish the per-category/per-matrix outcomes with links to retained
  evidence and exact tested SHA. Separate completed measurements from open
  acceptance gates. Documentation updates form a checkpoint after verification;
  commits, merges or publication require the applicable owner authorization.
- [ ] Leave the next smallest authorized action in this plan/agent log.
  A failed or blocked case names its evidence and required owner decision,
  access or bounded repair. Do not write “host/device acceptance complete”
  while HT-7, any mandatory matrix cell, instrumentation gate, tail/transition
  contract or other required acceptance case remains blocked/unmeasured.

## Owner and external gates remaining at planning handoff

1. Separate authorization for execution and any controlled test-host/diagnostic
   implementation; available hardware, host licenses and device access.
2. Revalidated released-OS floor, concrete family corners and resolution of
   any conflict with older ADR device/OS snapshots.
3. Development team/provisioning, approved identifiers and demonstrably
   installable container packaging; no distribution decision is implied.
4. Input-unavailable output policy; diagnostic access needed to measure
   counters, bounds and publication behavior; any missing contract fixes found
   by tests, including tail metadata, discontinuity or offline automation.
5. Separate ADR-011 state implementation before HT-7 can run.
6. Optional product CPU/headroom/instance-count budgets, and owner listening
   judgment of real automation/bypass recordings. Missing numeric budgets do
   not block recording measurements and never authorize invented thresholds.

## Checkpoint (2026-09-21)

The corrected HT-3 harness remains blocked by unavailable Apple runtime
services: CoreSimulatorService refused connection and CoreDeviceService timed
out during destination discovery. No HT-3 rerun or new acceptance conclusion
was claimed. Independently, Task 1's portable Release baseline passed 8/8
CTest suites and the DSP source-drift check matched 7 files; the separate
unsigned AU Debug compile exited 0. The next smallest action is to restore a
runnable destination and rerun the focused corrected `{4096}` case. Task 0's
device/OS/host matrix and execution authorization remain incomplete, and all
broader acceptance gates remain open.
