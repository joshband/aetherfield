# Finding: the AU does not accept host parameter writes

**Date:** 2026-09-24
**Severity:** blocks HT-7 and HT-9; qualifies HT-10's recorded parameter values
**Status:** behaviour confirmed by experiment; **mechanism not determined**
**Raised by:** building the REAPER automation harness (`scripts/ht_reaper/`)

## Summary

`AetherfieldAUExtensionMacOS` exposes its three parameters to the host correctly
by **name and range**, but:

- every host **read** of Decay/Damp/Mix returns `0.0`, regardless of the value the
  AU believes it holds;
- every host **write** to Decay/Damp/Mix has **no effect on rendered audio**.

The AU otherwise works: it instantiates, renders, and produces a reverb tail.
It simply renders with its internal defaults and ignores the host.

## Evidence

All renders 48 kHz stereo 32-bit float, REAPER 7.80 / macOS 26 arm64, via
`scripts/ht_reaper/bridge.py`. Raw files under
`artifacts/host-device/ht-macos-2026-09-24/parameter-defect/`.

### 1. Reads are dead, structure is fine

```
p0 Decay  norm=0.000000 raw=0.000000 range=[0.000..1.000] fmt="0.0000"
p1 Damp   norm=0.000000 raw=0.000000 range=[0.000..1.000] fmt="0.0000"
p2 Mix    norm=0.000000 raw=0.000000 range=[0.000..1.000] fmt="0.0000"
```

Names and ranges are correct, so the parameter tree *is* published. The values
are not. Expected initial values are Decay 0.5 / Damp 0.0 / Mix 1.0 — both from
`buildParameterTree`'s `decay.value = 0.5` assignments and from
`_lastDecay/_lastDamp/_lastMix`, which `initWithComponentDescription` seeds with
exactly those numbers before `buildParameterTree` runs.

Identical results via `TrackFX_GetParamNormalized`, `TrackFX_GetParam` and
`TrackFX_GetFormattedParamValue`, before and after a render (so this is not an
"allocate render resources first" ordering effect).

### 2. Writes do not reach the DSP

Four renders of the same input through the same instance, changing only
parameters between them:

| render | host action | payload SHA-256 (12) |
|---|---|---|
| `mix_default` | none | `cf21adebd728` |
| `mix_zero` | `SetParamNormalized(Mix, 0.0)` | `cf21adebd728` |
| `mix_one` | `SetParamNormalized(Mix, 1.0)` | `cf21adebd728` |
| `decay_high` | `SetParamNormalized(Decay, 0.95)` | `cf21adebd728` |

**Byte-identical.** `Mix = 0.0` means fully dry; the output is not dry (it differs
from the input at max amplitude 1.0 and carries a tail of 0.0037 at t = 2.0 s).
The DSP never saw any of these writes.

### 3. Control: the same code path works on another AU

The identical harness, API calls and render settings against Apple's
`AUMatrixReverb` (legacy in-process AU):

```
p0 Dry/Wet Mix        norm=1.000000      <- real value, not 0
set p0=0.0 -> read 0.000000              <- write sticks
set p0=1.0 -> read 1.000000              <- write sticks
```

| render | payload SHA-256 (12) | |
|---|---|---|
| `ctrl_p0_min` (Dry/Wet 0) | `fb80096ef982` | **identical to the input signal's own payload hash** — perfect dry passthrough |
| `ctrl_p0_max` (Dry/Wet 1) | `a57398a5f326` | wet, tail present |

So REAPER's parameter API, the harness scripts, the render configuration and the
32-bit-float render format are all correct. The defect is specific to
Aetherfield's AU.

Caveat on the control's strength: Aetherfield is the only audio **AUv3
out-of-process** extension installed on this Mac (`find /Applications -name
'*.appex'`), so the control is an in-process v2 AU. This experiment therefore
cannot distinguish "a defect in Aetherfield's AU" from "a defect in the
host↔AUv3-extension parameter path generally". The project has an existing
precedent for the latter class — the confirmed zero-frame `pullInputBlock`
skip in Apple's out-of-process render proxy, cross-checked against two
third-party AUv3 extensions on iOS. Resolving which applies here needs the same
kind of third-party AUv3 cross-check, on a machine that has one.

## What was eliminated

**The `parameterTree` getter override is not the cause.** `AetherfieldAudioUnit.mm`
implements `- (AUParameterTree *)parameterTree` rather than assigning
`self.parameterTree`. The macOS SDK header documents the override as the intended
approach — *"Subclassers should implement the parameterTree getter to expose
parameters to hosts"* (`AUAudioUnit.h`) — and the host does receive correct names
and ranges through it. Recorded so this is not re-investigated.

## Concrete suspect, unverified

In `buildParameterTree`:

```objc
decay.value = 0.5;
damp.value  = 0.0;
mix.value   = 1.0;

_parameterTree = [AUParameterTree createTreeWithChildren:@[decay, damp, mix]];

_parameterTree.implementorValueObserver = ^(AUParameter *p, AUValue v) { ... };
_parameterTree.implementorValueProvider = ^AUValue(AUParameter *p) { ... };
```

The three `.value` assignments happen while each `AUParameter` is still an orphan —
before it belongs to a tree, and before either implementor block exists. If an
orphan parameter's value does not survive into the tree that is published to the
host, the host-side tree would cache `0.0` for all three, which is exactly what is
observed. It would also make the very first host write of `0.0` a no-op (setting a
parameter to the value it already holds), though it does not by itself explain why
the later writes of `1.0` and `0.95` were also inert.

**This is a hypothesis, not a diagnosis.** It has not been tested, and it does not
account for all of the evidence.

## Consequences

**HT-7 (state save/recall) cannot be run.** Baseline and restored renders would
both use internal defaults, so the round-trip would pass vacuously. Worse,
`getState` serialises `_lastDecay/_lastDamp/_lastMix` — the same cache that reads
`0.0` — so the persisted payload would record `decay: 0, damp: 0, mix: 0`
irrespective of the actual configuration.

**HT-9 (multi-instance isolation) cannot be run.** The two instances would hold
identical default parameters, so a match between them proves nothing about
isolation.

**HT-5 and HT-6 are unaffected and were executed.** HT-5's gate is bypass
behaviour, which REAPER handles host-side; HT-6's gate is that reset clears state,
which holds under the AU's own defaults. Both PASS — see `manifest.json`.

**HT-10's recorded parameters are not supported by this evidence.** Its manifest
records "Decay/Damp/Mix ≈ 0.499 … set via precision mouse drag and confirmed via
the on-screen readout". Drags on the extension's own generic view are in-process
and may well have reached the DSP where host API writes do not, so this is not
necessarily wrong — but it is unverified, and the on-screen readout alone does not
establish it. **HT-10's own gate is unaffected:** it tested determinism across
repeated renders at a fixed setting, which holds whatever that setting was.

## Next step (requires authorization)

Determining the mechanism means instrumenting the extension's
`implementorValueObserver`/`implementorValueProvider` and the `ParameterBridge`
UI-write drain, and rebuilding the AU. That is implementation work against
`src/auv3/` and `src/wrapper/`, outside anything currently authorized, and it is
**not** proposed or undertaken here.

Until it is resolved, HT-7 and HT-9 are blocked by a product defect, not by
tooling.

## Refined analysis (2026-09-24, code review — no rebuild performed)

Owner authorized investigating the mechanism (not fixing it). This session's
device-bridge shell is a sandboxed Linux VM with no Xcode, `xcodebuild`,
`pluginkit`, or REAPER access — it can read and edit files in the repository
but cannot rebuild the AU, install it, or run the host-side probes that
produced the evidence above. This section is static code review only; the
diagnostic instrumentation below is unbuilt and unrun.

**The "orphan `.value` assignment" suspect recorded above does not hold up
against the actual code.** `buildParameterTree`'s `decay.value = 0.5` etc.
happen before the tree/provider exist, as noted — but `implementorValueProvider`
does not read the `AUParameter`'s own internal value storage at all. It reads
`_lastDecay`/`_lastDamp`/`_lastMix`, three `std::atomic<float>` ivars that are
seeded to the correct defaults (0.5/0.0/1.0) in `-initWithComponentDescription:`,
*before* `buildParameterTree` even runs (`AetherfieldAudioUnit.mm:173-175`,
`:228`). If the provider block were being invoked against a live instance,
it would return 0.5 for Decay and 1.0 for Mix — not 0.0. The orphan-value
theory predicts the wrong failure mode; it should be retired as the leading
suspect.

**Three concrete, code-grounded alternatives fit every observed symptom exactly,
and are indistinguishable from source alone:**

1. **`weakSelf` resolves to `nil` when the host/XPC layer invokes these
   blocks.** Both blocks capture `self` only via `__weak`
   (`AetherfieldAudioUnit.mm:361`); if the object satisfying host parameter
   queries over AUv3's out-of-process XPC boundary is not the same instance
   whose strong references keep it alive during rendering, `strongSelf` is
   `nil` and both blocks bail out unconditionally, for every parameter and
   every call — the observer at `return;` (a `void` block, so the write is
   simply dropped) and the provider at `return 0.0F;`.
2. **`parameter.address` does not match `Parameter::Decay/Damp/Mix` (0/1/2)
   by the time the block runs.** The `switch` has no `default:` case; any
   address that doesn't match one of the three literal cases falls through
   to the same `return 0.0F;` after the switch (`:379`). If the `AUParameter`
   object the host-side XPC round-trip hands back to the block is a
   reconstructed proxy that doesn't preserve `.address` faithfully, every
   call would fail identically regardless of `strongSelf`.

3. **The host/XPC layer never invokes these blocks at all.** Nothing in the
   host-side evidence distinguishes "the block ran and bailed out" from "the
   block was never called". This is not an exotic possibility: it is precisely
   the shape of the defect this project has *already confirmed* in Apple's
   out-of-process AUv3 proxy, where `pullInputBlock` is silently not delivered
   into the extension while the host still sees `noErr`. On that precedent it
   is arguably the **leading** candidate, not a fallback.

   Hypotheses 1 and 3 are behaviourally identical from the host's side but have
   entirely different remedies, so they must not be conflated.

All three produce **exactly** the observed evidence: names/ranges correct (both
come from the tree's static structure, not these blocks), every read exactly
`0.0` regardless of the parameter's true default, every write inert. All three
are also consistent with this project's own established precedent that Apple's
out-of-process AUv3 proxy silently fails to deliver certain calls into the
extension (the confirmed zero-frame `pullInputBlock` skip) — this would be a
second, independent instance of the same class of problem, not a coincidence
requiring a new kind of explanation.

**Evidence this narrows toward the AU-layer glue, not the bridge or DSP:**
`ParameterBridge::drain()` — the same call the observer block feeds via
`_bridge->writeUi(...)` — is independently verified correct by PB-1…PB-8
(`docs/testing.md` "PB-1…PB-8"), entirely at the portable C++ level with no
`AUParameterTree`/XPC involved. If the defect were downstream of these two
blocks, PB-1…PB-8 would already have caught it. The defect is bounded to
`implementorValueObserver`/`implementorValueProvider` themselves, or to how
the host/XPC layer invokes them — not to `ParameterBridge`, `DiffusionStereoPath`,
or the render path.

**Diagnostic instrumentation added to `AetherfieldAudioUnit.mm` (2026-09-24,
uncommitted, unbuilt):** `NSLog` calls in both blocks reporting
`parameter.address`, whether `strongSelf` resolved, and which `switch` case
(if any) matched. Marked `[DIAG]` and commented as temporary. **Next step for
a session with real Mac/Xcode/REAPER access:** rebuild
`AetherfieldHostMacOS` (Release), reinstall per the procedure in
`docs/agent-log.md`'s HT-10 entries, insert the AU in REAPER, run one
`TrackFX_SetParamNormalized`/`GetParamNormalized` probe (the existing
`scripts/ht_reaper/` harness or `H.set_params`/manual REAPER Actions both
work), and tail `log stream --predicate 'process == "REAPER" OR
subsystem == "com.aetherfield"' --style compact` (adjust predicate to
whatever actually carries this extension's `NSLog` output out-of-process —
confirm empirically, don't assume) during the call. **How to read the result** — the candidates give
distinct, unmistakable signatures:

| What the log shows during a host parameter get/set | Conclusion |
|---|---|
| no `[DIAG]` lines at all | **hypothesis 3** — the proxy never delivers the call into the extension |
| `strongSelf=NIL` | **hypothesis 1** — the weak capture does not resolve for the instance the host queries |
| `strongSelf=valid` + `matched no case`, with an unexpected `address` | **hypothesis 2** — `.address` does not survive the round trip |
| `strongSelf=valid` + `matched target=0/1/2`, yet audio still does not change | none of the three — the blocks are fine and the fault lies downstream of `writeUi`, which would contradict PB-1…PB-8 and needs its own investigation |

Record which signature actually appears before proposing any fix; the last row
in particular would invalidate the bounding argument above. **No fix should be implemented from this alone** —
per the owner's authorization, this investigation stops at diagnosis.
