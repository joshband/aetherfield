---
id: "ADR-012"
status: accepted
accepted: "2026-09-18"
implementation: "none; design-only, no test/host/device/CI implementation"
review: "owner-accepted-2026-09-18"
review_document: null
depends_on: [ADR-001, ADR-007, ADR-008, ADR-009, ADR-010]
---

# ADR-012 — Host/device acceptance-test catalog: methodology for HT-1…HT-12, device/OS scope policy, and tooling scope

## Review summary

- **Decision:** convert [ADR-010](ADR-010-device-lifecycle-matrix.md)'s twelve
  named-but-undesigned host/device acceptance categories, HT-1 through HT-12,
  into a methodology-level design — for each category, which configuration axes
  actually matter, what a recorded result must contain to count as evidence
  under this project's existing discipline, and what is deliberately left to
  execution time — plus a device/OS scope policy (owner-decided: (D)) and a
  proposed tooling scope for Apple's `auval`.
- **Why:** ADR-010 accepted the matrix and the lifecycle contract but stated
  plainly of its own HT list that "none of them is designed, scoped in detail,
  or run by this ADR," and its "Remaining decisions and later evidence" defers
  the whole question: "An eventual verification plan must run HT-1 through
  HT-12 above on named devices/hosts/OS versions once the minimum-version
  decision is made; none of that evidence exists today." The minimum-version
  decision has since been made (ADR-010's accepted rolling "current major
  version minus one"), so the blocking dependency on that half is discharged
  and the design work is now doable — but the *device/chip-tier* half is still
  explicitly the owner's, and this record does not take it from them.
- **Consequence:** a future bounded wrapper implementation plan inherits twelve
  designed test categories with stated axes and stated evidence shapes, rather
  than twelve titles it would have to re-derive; and the one genuine
  product/cost decision hiding inside "run HT-1…HT-12 on named devices" — how
  many device tiers and OS versions constitute sufficient coverage — is
  surfaced as an explicit alternatives table for the owner instead of being
  settled by whatever hardware happens to be on a desk.
- **Uncertainty:** this record produces **zero** evidence. HT-7's
  methodology was drafted before [ADR-011](ADR-011-state-schema.md) (state
  schema) reached its own final acceptance; both ADRs are now accepted
  together on 2026-09-18, and HT-7's schema-dependent half must still be
  reconciled against ADR-011's actual accepted content — including its
  atomic-restore decision — as a follow-up correction, not assumed
  complete by this record's original drafting. The device/OS scope policy
  below was a recommendation against a real cost tradeoff at drafting
  time; (D) is now the owner's accepted choice, though which physical
  tiers it applies to remains open.

**Status: Accepted (2026-09-18).** The owner accepted this record's
methodology for HT-1 through HT-12 and selected device/OS scope policy
**(D)** — corners only (oldest supported OS on the oldest in-scope chip
tier, newest OS on the newest tier) for most categories, extended with the
weakest in-scope chip tier specifically for HT-11 and HT-12 — over (A)'s
plain corners, (B)'s per-OS-major sweep, and (C)'s full cross-product.
This selects a coverage *shape* only: **which physical device tiers are in
scope remains the owner's separate, still-open decision**, exactly as
ADR-010's "Revisit when" named it. Accepting this record authorizes no
test, host, device, CI, or wrapper implementation. HT-1 through HT-12
remain named-but-undesigned-for-execution categories; this ADR adds
methodology and a scope policy, not evidence. No iOS device, simulator, or OS version has ever
run any Aetherfield code (ADR-001; docs/testing.md: "cross-platform builds, iOS
compilation, simulator/device hosting, sample-rate lifecycle and render-thread
instrumentation remain unverified"), and nothing below changes that. Acceptance
of this record would authorize no code and no device time; a separately
authorized, bounded implementation plan is still required afterward, exactly as
[ADR-007](ADR-007-auv3-integration-comparison.md) required and as
[ADR-008](ADR-008-parameter-event-bridge.md),
[ADR-009](ADR-009-production-bus-policy.md) and
[ADR-010](ADR-010-device-lifecycle-matrix.md) each likewise required for their
own subject matter.

## Context and scope

[ADR-010](ADR-010-device-lifecycle-matrix.md) (accepted 2026-09-18) closed the
supported-rate and lifecycle-mapping questions and, in its "Future host/device
acceptance test categories" section, named twelve categories "at the
granularity of this project's existing DS-1…13/NS-1…11/PT-1…9 convention,
listed here because this matrix implies they must eventually exist — none of
them is designed, scoped in detail, or run by this ADR." Those twelve
categories, verbatim from ADR-010, are the entire subject of this record:

> - **HT-1** — cold instantiation and teardown (`allocateRenderResourcesAndReturnError:` / `deallocateRenderResources` cycle, repeated)
> - **HT-2** — reconfiguration (format/rate change while instantiated; `prepare`-failure rollback observed at the AU boundary)
> - **HT-3** — variable- and zero-length render-call handling, including block sizes beyond the `{1,13,64,512,3}` fixed partitions and DS-11's ragged `{7,29,3,211,5}` partition this project has actually measured
> - **HT-4** — input-pull failure / host underrun handling
> - **HT-5** — bypass and tail behavior (depends on [ADR-009](ADR-009-production-bus-policy.md)'s bypass decisions)
> - **HT-6** — reset and aggregate-fault recovery, both host-triggered (`-reset`) and self-triggered (DS-B Task 3's next-block path), including the cross-thread reset-concurrency question named above
> - **HT-7** — state save/recall (`fullState` round trip) — blocked on the still-unassigned state-schema ADR; not runnable until that lands
> - **HT-8** — dense or conflicting host automation/parameter events (depends on [ADR-008](ADR-008-parameter-event-bridge.md))
> - **HT-9** — multiple concurrent instances
> - **HT-10** — offline (non-realtime) render repeatability
> - **HT-11** — realtime allocation/locking audit on the actual render callback, on-device
> - **HT-12** — callback-deadline / worst-case timing measurement on named devices

**This ADR does not renumber, merge, split, or redefine the scope of any HT
category.** It adds a methodology subsection under each and nothing else. It
does not reopen ADR-010's supported sample-rate set ({48 kHz, 44.1 kHz}), its
deployment-target policy, its lifecycle mapping table, or any of its named
gaps; it does not reopen ADR-008's bridge design, ADR-009's bus/bypass
decisions, or ADR-001's portable-core boundary. It decides no UI, signing,
identifier, licensing, or CI question.

This is the same kind of work [ADR-005](ADR-005-evaluation-fixture.md) already
did for this project's host-side evaluation fixture: fix the concrete,
reproducible inputs a later measurement pass will be run against, while
"**never a measurement**, because Aetherfield has still measured nothing about
reverb" was true at the time of writing. The precedent is deliberate. ADR-005
fixed `N`, the delay set and the fixture rates so that NS-1…NS-11 and
PT-1…PT-9 could later be run against real numbers; this ADR fixes axes,
evidence shapes and scope policy so that HT-1…HT-12 can later be run against a
real wrapper. In both cases the record itself runs nothing.

## Evidence basis

No measurement, build, device test, or host test was performed for this ADR,
and none is authorized by it. Every factual claim below traces to a file read
while drafting: `docs/decisions/ADR-010-device-lifecycle-matrix.md`,
`docs/decisions/ADR-008-parameter-event-bridge.md`,
`docs/decisions/ADR-009-production-bus-policy.md`,
`docs/decisions/ADR-005-evaluation-fixture.md`, `docs/testing.md`, and
`docs/phases/phase1-ds-integration-plan.md`.

| Source | What it grounds in this ADR |
|---|---|
| ADR-010, "Future host/device acceptance test categories" | The twelve HT titles and their scope, quoted verbatim above and not redefined here. |
| ADR-010, "Revisit when" | The device/chip-tier question being the owner's, still open: "the owner sets a device/chip-tier scope." |
| ADR-010, Status | The accepted rolling **"current major version minus one"** deployment floor — iOS/iPadOS 25+ as of 2026-09-18 — explicitly "a **relative policy, not a fixed number**," to be re-verified when the implementation plan is written. |
| ADR-010 (b) | The supported sample-rate set is exactly **{48 kHz, 44.1 kHz}**, on evidence availability, not market judgment. |
| ADR-010 (c); docs/phases/phase1-ds-integration-plan.md Task 2; docs/testing.md DS-11 | The measured whole-path block-partition coverage: the fixed set `{1,13,64,512,3}` and DS-11's ragged `{7,29,3,211,5}`; ADR-010's own reading that "'beyond 512' is the honest boundary of measured territory, not 'beyond 64'." |
| docs/testing.md, S2/NS-7 record | The **bare FDN alone** was additionally rendered under `{1, 13, 64, 512, 977}`, its peak output magnitude (≈23.6×) identical across all five with `nonFiniteCount() == 0`. This is FDN-layer coverage of a peak diagnostic, not whole-path bit-identity — see the note under HT-3. |
| docs/phases/phase1-ds-integration-plan.md Task 3; docs/testing.md DS-B Task 3 | The self-triggered recovery path HT-6 must exercise: a pending reset "clears every input/output allpass, FDN and automation, clears current latches, and preserves cumulative counts before rendering its first sample"; zero-frame processing returns immediately and consumes nothing (Luna's traced `if (count == 0 || !state_) return;` guard ordering). |
| docs/testing.md DS-10 | Measured whole-path time-to-exact-silence for its declared fixture: **321,914 samples @48 kHz**, **297,234 @44.1 kHz**, over a 1.4M-sample render. One concrete instance of ADR-009's bounded-tail argument, not a constant. |
| docs/testing.md DS-11, DS-12 | Determinism and cost evidence that already exists **on the macOS arm64 host only**: repeated identical-script renders bit-identical; 16 bracket cascades bit-identical pairwise; global allocation-counter delta 0 across warmed steady-state runs; wall-clock ≈67–69 ns/sample (full wrapper) vs. ≈26–27 ns/sample (bare FDN), recorded "as observation only," explicitly not a CPU budget. |
| docs/testing.md, closing convention (final paragraph) | The evidence-recording rule this catalog's pass/fail shapes are built on, quoted in full below. |
| ADR-008 §2/§3/§5 | What HT-8 must actually stress: nine fixed mailbox cells, at most three wait-free writes per callback, unconditional last-write-wins coalescing, no chosen per-callback event cap, and a Controller scheduling latency that is "a reasoned estimate, not a proven bound." |
| ADR-009 §2/§3 | What HT-5 must actually check: bit-exact dry passthrough on `shouldBypassEffect`; bounded (derivable, finite) tailTime recomputed when Decay/Damp change; no hard discontinuity on un-bypass; and the still-unresolved `canProcessInPlace` question for the decided stereo-in/stereo-out bus. |
| ADR-001; docs/testing.md "Limits and future work" | That no iOS device, simulator, or OS version has ever run any Aetherfield code, and that host verification to date is macOS arm64 only. |

## Relationship to this project's existing evidence discipline

**This catalog produces zero evidence, and no part of it may be read as
coverage.** It converts ADR-010's named-but-undesigned categories into a design
that this project's existing host CTest-style discipline could execute against
*once a wrapper exists and implementation is separately authorized* — nothing
more. Every HT-N below describes a test that does not exist, for a wrapper that
does not exist, on devices that have not been chosen.

The discipline the designs below are written against is already stated in this
repository and is cited, not paraphrased. `docs/testing.md`'s closing
convention:

> For all future tests: use named cases, print the failed condition, and return
> nonzero on failure even in Release. Preserve seeds, parameter settings,
> sample rates and block sequences with evidence. Missing measurements are
> **unmeasured**, never passes. Report output overshoots and safety
> interventions rather than hiding them by clipping or regenerating reference
> data. Exact byte comparisons apply only where the operation/toolchain
> contract supports them; nonlinear or transcendental implementations need
> justified numerical tolerances.

and the AUv3 evaluation plan's own instruction for exactly these categories
(`docs/superpowers/plans/2026-09-18-auv3-integration-evaluation.md`):

> Record configurations and failures, not a generic compatibility claim.

and the rule ADR-010 restates from ADR-003/ADR-005 when it declines to treat
its own source-inspection multi-instance finding as a result: "never claim
evidence that does not exist."

Three consequences follow for every HT category, and are not repeated under
each one:

1. **An HT record names its exact configuration or it is not a record.** Device
   model and chip, OS build (not just major version), host application and its
   version, sample rate, block size or block sequence, parameter settings, and
   build configuration. "Works on iPad" is not an HT result; it is a generic
   compatibility claim of precisely the kind the plan line above forbids.
2. **An unrun axis is `unmeasured`, never an implied pass.** A category run at
   48 kHz only is recorded as run at 48 kHz only, in the same way DS-8 is
   recorded in `docs/testing.md` as "measured at 48kHz only; a 44.1kHz leg is a
   small follow-up" rather than quietly generalized.
3. **Observations that are not gates are labelled as observations.** This
   project already does this — DS-6's echo density, DS-7's coherence, and
   DS-12's ≈67–69 ns/sample figure are all recorded ungated. Several HT
   categories below (notably HT-12) are *measurement* categories whose numbers
   have no defensible pass threshold until the owner sets one; those say so.

## Configuration-axis vocabulary

The five axes referenced per category, defined once:

| Axis | Meaning here | Notes |
|---|---|---|
| **Device/chip tier** | The physical device class and SoC generation (e.g. an older A-series iPhone vs. a current M-series iPad). | The axis ADR-010 leaves to the owner. Matters for anything thermal, deadline-bound, or memory-pressure-bound; largely irrelevant to purely logical lifecycle behavior. |
| **OS version** | The iOS/iPadOS major version, and for a recorded result the exact build. | Governed by ADR-010's rolling "current major version minus one" floor. Matters where Apple's own AUv3 host/runtime behavior can differ; irrelevant where the behavior under test is entirely inside this project's own C++. |
| **Sample rate** | One of ADR-010 (b)'s supported set, {48 kHz, 44.1 kHz}, plus deliberately *unsupported* rates where the test's subject is rejection. | Only two supported rates exist, so "both rates" is a cheap axis and should be the default for anything rate-derived. |
| **Block size** | The host's `frameCount` per render call, including 0, and the sequence of them. | The axis HT-3 owns. Also a cheap axis: it costs render time, not hardware. |
| **Host app** | The AUv3 host application driving the extension (plus `auval`, see Tooling policy). | Matters wherever *host* behavior — not plugin behavior — is the variable: bypass semantics, automation delivery, offline bounce, state restore, underrun handling. Irrelevant where the plugin's own code path is the entire subject. |

Two axes (sample rate, block size) cost only render time; two (device/chip
tier, OS version) cost hardware and lab time; one (host app) costs
licences and setup effort. The per-category axis lists below are written with
that asymmetry in mind: **the cheap axes are swept broadly, the expensive ones
only where the category's actual failure mode depends on them.** Defaulting
every category to "test everywhere" would be both dishonest about what each
test can discriminate and needlessly expensive.

## Catalog overview

| Category | Device/chip tier | OS version | Sample rate | Block size | Host app | Kind of evidence |
|---|---|---|---|---|---|---|
| HT-1 cold instantiation/teardown | Minor | Minor | Both | One nominal | One, plus `auval` | Gated (pass/fail) |
| HT-2 reconfiguration | Minor | Minor | Both + unsupported | One nominal | ≥2 (rate-change behavior differs) | Gated |
| HT-3 variable/zero-length blocks | No | No | Both | **Primary axis** | One; host only to observe real `maximumFramesToRender` | Gated (bit-identity) |
| HT-4 input-pull failure/underrun | Minor | Minor | One | Nominal + stress | **Primary axis** | Gated (no fault, no crash) + recorded |
| HT-5 bypass and tail | No | Minor | Both | One nominal | **Primary axis** | Gated (bit-exact dry) + recorded (tail) |
| HT-6 reset and fault recovery | No | No | Both | Includes 0 | One, plus a host that resets on transport stop | Gated |
| HT-7 state save/recall | No | Minor | Both | — | **Primary axis** | Gated (atomicity, round-trip bit-identity) + recorded (timing/context) |
| HT-8 dense/conflicting automation | Minor | Minor | One | Nominal + small | **Primary axis** | Recorded + one gated bound |
| HT-9 multiple instances | **Primary axis** | Minor | One | One nominal | One | Gated (isolation) + recorded (scaling) |
| HT-10 offline repeatability | **No** | No | Both | Host's own | **Primary axis** | Gated (bit-identity) |
| HT-11 realtime allocation/locking audit | Minor | Minor | Both | Nominal + 0 | One | Gated (zero allocations, zero locks) |
| HT-12 callback-deadline/worst-case timing | **Primary axis** | Minor | Both | **Several** | One | **Recorded, ungated until the owner sets a budget** |

"Minor" means: record the axis faithfully in every result, but do not multiply
the run matrix by it — one tier/version is enough unless a failure appears, at
which point the axis becomes primary for the investigation.

## HT-1 — cold instantiation and teardown

Repeated `allocateRenderResourcesAndReturnError:` / `deallocateRenderResources`
cycles on a freshly instantiated AU, and repeated full instantiate/destroy
cycles, with rendering in between.

**Configuration axes.** Sample rate: both supported rates, because
`prepare()` re-derives `mᵢ`, `γ₀`, `γ_π`, `gᵢ` and `aᵢ` per ADR-003 (d) and the
two rates produce different realized delay sets (48 kHz input `{47,103,223,479}`
vs. 44.1 kHz `{43,97,199,439}`, per the DS-B Task 1 record) — a leak or a
stale-state bug can hide at one rate and not the other. Block size: one
nominal value; this category is not about block size. Device/chip tier and OS
version: **minor** — the code path is this project's own C++ plus two Apple
lifecycle entry points, and nothing in it is plausibly chip-dependent; record
the tier, do not sweep it. Host app: one, plus `auval` (which exercises
instantiate/allocate/deallocate as part of its own structural pass).

**Pass/fail evidence shape.** A gated result. It must record: the cycle count
actually run (N cycles is a stated number, not "repeatedly"); resident-memory
or allocation-instrumented delta across the cycles, with the measurement
method named; whether each `allocateRenderResourcesAndReturnError:` returned
`YES` with a null error; and — the load-bearing one for this project — whether
`nonFiniteCount()` behaved per ADR-010's lifecycle table across the cycle.
ADR-010 names the specific unresolved hazard this category exists to expose:
if the wrapper reconstructs the C++ `DiffusionStereoPath` at
`deallocateRenderResources` rather than keeping it alive, "the cumulative fault
counter — which DS-B Task 3 requires to survive `reset()`/re-`prepare()` —
would be silently lost." **HT-1 must therefore report the counter's value
before and after a deallocate/reallocate cycle explicitly**, whichever way the
implementation plan resolves that gap, so the record shows which behavior was
actually built. Per the discipline above, a growth figure with no stated
measurement method is `unmeasured`, not a pass.

**What is explicitly NOT decided here.** The cycle count N; the
memory-measurement tool; which physical devices; which OS point-release; and
the resolution of ADR-010's deallocate/reconstruct gap itself, which is an
implementation-plan decision this test only *observes*, never settles.

## HT-2 — reconfiguration

Format or sample-rate change while instantiated, and `prepare`-failure rollback
observed at the AU boundary rather than inferred from the core's own unit
tests.

**Configuration axes.** Sample rate: **both supported rates plus at least one
deliberately unsupported rate**, because the rejection path is half the
category. ADR-010 fixed the AU-layer mapping precisely: on an unsupported
rate the wrapper "must translate that boolean/exception outcome into a
populated `NSError` and return `NO`, without attempting a silent fallback (e.g.,
forcing the request to 48 kHz)." ADR-010 is equally explicit that a fallback
"would violate ADR-003 (d) rule 7's transactional guarantee by pretending the
requested configuration succeeded" — so this test's real subject is the absence
of a silent fallback, not the presence of an error code. Channel-count/bus-layout
rejection is named by ADR-010 as the same `prepare`-time failure category and
belongs here too, against ADR-009's decided stereo-in/stereo-out layout. Host
app: **at least two**, because how a host requests a rate change (and whether
it does so without an intervening teardown) is host behavior, not plugin
behavior. Device/chip tier and OS version: minor. Block size: one nominal.

**Pass/fail evidence shape.** A gated result recording, per attempted
reconfiguration: the requested format, the returned `BOOL`, the `NSError`
domain/code/description actually populated, and — critically — a rendered
comparison proving the *prior* configuration still produces its original output
after the rejected change. ADR-003 (d) rule 7's guarantee is that a failed
`prepare` "changes nothing... a prior valid preparation remains valid and
usable"; at the AU boundary that is only evidenced by rendering the same
deterministic script before and after the failed call and comparing. Bit
identity is the right comparison here (same binary, same host, same
configuration), consistent with DS-11's existing bit-identity discipline for
the core.

**What is explicitly NOT decided here.** Which unsupported rate to probe with;
which two hosts; how many reconfiguration cycles; whether an unsupported-rate
failure additionally surfaces a user-visible diagnostic, which ADR-010 already
flagged as an open UX question entangled with the deferred UI decision and
which this record does not touch.

## HT-3 — variable- and zero-length render-call handling

Block sizes beyond the partitions this project has measured, including the
documented zero-length call.

**Configuration axes.** **Block size is the primary axis and essentially the
only one.** Sample rate: both, cheaply. Device/chip tier: **no** — block-size
partition invariance is a property of this project's own arithmetic and its
block-boundary automation model, not of any SoC. OS version: **no**, for the
same reason. Host app: one, and only to *observe* what `maximumFramesToRender`
real hosts actually request; the invariance itself is better driven
synthetically than by hoping a host emits an interesting sequence.

The measured baseline, re-derived rather than recalled: whole-path bit-identity
is recorded for the fixed partition set `{1,13,64,512,3}` (DS-B Task 3;
`docs/phases/phase1-ds-integration-plan.md` Task 2) and DS-11's ragged
`{7,29,3,211,5}`. ADR-010 (c)'s own reading is the honest one — the ragged set's
largest step is 211, so "'beyond 512' is the honest boundary of measured
territory, not 'beyond 64'." **A larger block size that has been exercised
elsewhere, noted rather than smoothed over:** at S2/NS-7 the *bare FDN* was
rendered under `{1, 13, 64, 512, 977}` and its recorded peak output magnitude
(≈23.6×) was identical across all five, with `nonFiniteCount() == 0`
(`docs/testing.md`). That differs from ADR-010's statement in two ways at once
and contradicts it in neither: it is the FDN layer, not the whole path, and it
is a peak-magnitude diagnostic, not the whole-render bit-identity comparison
ADR-010 is enumerating. So 977 is *not* covered whole-path partition-invariance
evidence, and HT-3 must close the whole-path side rather than cite NS-7 as if
it had.

**Pass/fail evidence shape.** A gated bit-identity result, in the exact form
DS-11 already uses: one deterministic script rendered whole-buffer, then
rendered again under each partition, compared float-bit-identically, with the
partition sets written out literally in the record. It must additionally
record: the zero-length case behaving per DS-B Task 3's global constraint — "a
zero-frame call neither processes audio nor consumes a pending reset" — which
means a zero-frame call interleaved into a partition sequence *while a reset is
pending* must leave the pending reset intact, and the post-sequence output must
still be bit-identical. It must also record the largest `frameCount` actually
exercised and the largest `maximumFramesToRender` any tested host actually
declared; if the second exceeds the first, that gap is recorded as `unmeasured`
territory rather than assumed covered. ADR-010's inference is explicitly an
inference — "the architecture's block-boundary-only automation model implies
the property generalizes, but that is an inference, not a measurement" — and
HT-3 exists to replace it, not to restate it.

**What is explicitly NOT decided here.** The specific new partition sets (a
concrete proposal such as "the existing two sets plus at least one ragged
sequence whose maximum exceeds the largest observed host
`maximumFramesToRender`" is a reasonable starting point for the implementation
plan, not a decision made here); the number of trials; the script content.

## HT-4 — input-pull failure / host underrun handling

The host's input-pull block returning an error, returning fewer frames than
requested, or the render callback overrunning its deadline such that the host
reports an underrun.

**Configuration axes.** **Host app is the primary axis**, because this
category's entire subject is host-side failure behavior; a single host
exercises a single host's idea of what an input-pull failure looks like.
Block size: nominal plus a stress value, since underruns are more reachable at
small blocks. Sample rate: one is sufficient — nothing in the failure path is
rate-derived. Device/chip tier and OS version: minor for the *logical* failure
handling, but note that *inducing* a genuine underrun (as opposed to a
synthetic input-pull error) is easier on a lower tier, which is a reason to
record the tier, not a reason to sweep it.

**Pass/fail evidence shape.** A gated result on the plugin's behavior, plus
recorded observations of the host's. Gated: on an input-pull failure the
wrapper must not render garbage, must not propagate a non-finite value, and
must not allocate or block on the render thread; `nonFiniteCount()` before and
after must be recorded. The interesting question this category must answer
explicitly — and which no accepted ADR currently decides — is **what the
wrapper outputs when input is unavailable**: silence, dry passthrough of
whatever the buffer contains, or the continuing wet tail. That is a genuine
product choice adjacent to ADR-009's bypass reasoning and is named in
"Remaining decisions" below rather than assumed here; HT-4's job is to record
what the built wrapper actually does and confirm it is deterministic and
fault-free, not to validate a policy that does not yet exist.

**What is explicitly NOT decided here.** The wrapper's chosen
input-unavailable output policy (see Remaining decisions); which hosts; how an
underrun is to be induced; how many trials.

## HT-5 — bypass and tail behavior

Depends on [ADR-009](ADR-009-production-bus-policy.md)'s bypass decisions, and
inherits its open items.

**Configuration axes.** **Host app is the primary axis**: `shouldBypassEffect`
is set by the host, and hosts differ in when they set it, whether they ramp
around it, and whether they keep pulling render while bypassed. Sample rate:
both, because the tail length this category measures is rate-dependent (DS-10
measured whole-path exact silence at 321,914 samples @48 kHz vs. 297,234
@44.1 kHz for its declared fixture). Block size: one nominal. Device/chip tier:
**no** — bypass correctness is logical, not thermal. OS version: minor.

**Pass/fail evidence shape.** Three distinct things, only two of them gatable.

- *Gated, bit-exact:* with bypass engaged, output equals input exactly. ADR-009
  §2 decided this at decision level ("the wrapper's output must be the
  unprocessed dry input (bit-exact passthrough)") and named the existing
  bit-exact precedent (`testSetMixToZeroBypassesToDryExactly`). Bit comparison
  is legitimate here because passthrough involves no transcendental arithmetic
  — consistent with testing.md's caveat that "exact byte comparisons apply only
  where the operation/toolchain contract supports them."
- *Gated, no-discontinuity:* un-bypass must not introduce the hard
  discontinuity ADR-009 §2 forbids. The evidence shape is a recorded
  maximum sample-to-sample delta across the transition, in the same form PT-7
  already uses for parameter sweeps (its measured 0.354 figure is the existing
  precedent for that measurement's shape, not a threshold to reuse).
- *Recorded, not gated:* the reported `tailTime` versus the actually measured
  time to exact silence, at the configured Decay/Damp. ADR-009 is explicit that
  tailTime is "a *derivable, finite* quantity for the currently configured
  Decay/Damp settings" and that DS-10's figure "is one concrete instance of
  this bound, not a fixed constant." So HT-5 records the pair (reported,
  measured) at several Decay/Damp settings and flags any case where reported <
  measured, which would be a real defect; it does not gate on a fixed number.

**What is explicitly NOT decided here.** ADR-009's own open items stay open and
are not resolved by designing a test for them: whether bypass keeps computing
the wet path (tail continuity at full CPU cost) or pauses it (CPU saved, tail
lost); whether a host-exposed "kill the wet tail instantly" control exists; and
`canProcessInPlace`'s eventual value for the decided stereo-in/stereo-out bus,
which ADR-009 §3 leaves to the bounded implementation plan. **HT-5's design
branches on those answers** — a tail-preserving bypass and a
tail-discarding bypass require different pass criteria for the un-bypass
continuity check — so this subsection specifies the axes and the evidence
shapes, and states that the criteria are selected once ADR-009's open items are
closed by the implementation plan.

## HT-6 — reset and aggregate-fault recovery

Both host-triggered (`-reset`) and self-triggered (DS-B Task 3's next-block
path), including the cross-thread reset-concurrency question ADR-010 names as
unassigned.

**Configuration axes.** Sample rate: both. Block size: several, and **must
include zero-length calls**, because the zero-frame interaction is part of the
contract under test. Device/chip tier: **no** for the logical contract.
OS version: **no** for the logical contract. Host app: one, plus specifically a
host that issues `-reset` on transport stop, since that is the realistic
trigger. The cross-thread sub-case is the exception to "device tier doesn't
matter": a race is more likely to be *observed* on a multi-core device under
load, but observing a race is not evidence of its absence, which is why the
evidence shape below leans on instrumentation rather than on repetition.

**Pass/fail evidence shape.** Gated, in three parts.

1. *Host-triggered `-reset`:* after `-reset`, the next nonempty block begins
   from exact whole-path silence (the DS-10 whole-path silence-in/silence-out
   bit-exactness already measured on the core is the model), **and the
   cumulative fault counter is unchanged**. ADR-010 adds this specifically at
   the wrapper level: "Wrapper must not clear the cumulative fault counter on a
   host-triggered reset either — the 'reported, never hidden' rule (DS-B global
   constraints) applies to the wrapper's exposed diagnostic surface." The
   record must print the counter before and after.
2. *Self-triggered recovery:* inject a non-finite value at the AU boundary and
   confirm DS-B Task 3's exact behavior through the wrapper — the faulting
   block is not reset mid-sample; the *next nonempty* block starts from exact
   whole-path silence; the cumulative count survives; and a zero-frame call
   interposed between the two does not consume the pending reset. These are the
   same assertions `tests/DiffusionStereoPathTests.cpp` already makes against
   the core (per the DS-B Task 3 record); HT-6's contribution is that they are
   made across the AU boundary, in a host, on a device.
3. *Cross-thread concurrency:* ADR-010 names this as genuinely unowned — "AUAudioUnit
   hosts may invoke `-reset` from a thread other than the render thread…
   Whether that requires a synchronization mechanism, and if so what, is
   unaddressed by any accepted ADR," and ADR-010 explicitly declines to assign
   it to ADR-008 or anywhere else. **A test cannot close a design gap.** HT-6's
   honest evidence shape for this part is therefore: run the concurrent case
   under a thread sanitizer or equivalent instrumentation and record the result
   as a *data race detected / not detected* observation with the tool and
   configuration named, never as "reset is thread-safe." A clean run of N trials
   is evidence about those N trials only.

**What is explicitly NOT decided here.** The ownership or resolution of the
cross-thread `reset()` question — it remains exactly as unassigned as ADR-010
left it; the instrumentation tool; the trial count; which host issues the
reset.

## HT-7 — state save/recall (`fullState` round trip)

**This category's methodology was drafted before ADR-011 (state schema)
reached its own final acceptance, and has now been reconciled against
ADR-011's actual accepted content**, both ADRs having been accepted
together on 2026-09-18. ADR-010 marked HT-7 "blocked on the
still-unassigned state-schema ADR; not runnable until that lands." That ADR
is now [ADR-011](ADR-011-state-schema.md), **accepted**, and its accepted
content — including that restore applies all three parameters atomically,
via a new `ParameterAutomation::setAll` entry point ADR-011 names but does
not implement — is now reflected below rather than treated as a
contingent proposal.

**Schema-independent axes.** **Host app is the primary axis**: when a host asks
for `fullState`, on which thread, and in what context it later restores
(instantiation-time restore versus a mid-session preset recall versus a session
reopen) is host behavior, and differs between hosts. OS version: minor.
Device/chip tier: no — serialization is not chip-dependent. Sample rate: both,
for one specific reason that is schema-independent — a state saved at one rate
and restored at the other exercises ADR-003 (d)'s rate-change contract and
ADR-010's rejection-not-clamping rule, whatever the schema turns out to be.

**Schema-independent evidence shapes.**

- *Round-trip timing and context:* record on which thread `fullState` is read
  and written by each tested host, and whether a restore can land while the
  render callback is active. ADR-011 reopened ADR-008's alternative (C) for
  the StateRestore role specifically: a restore no longer shares Host/UI's
  per-parameter mailbox cells at all, but is applied through a separate
  three-slot atomic tuple buffer and a single combined `setAll` publish.
  ADR-011 §4 gives the reconciled ordering: within one Controller drain,
  pending Host mailbox values are applied first, then a pending StateRestore
  tuple (if ready), then pending UI mailbox values — reproducing ADR-008's
  original intent (a restore is protected from a *stale* pending Host write,
  but a same-pass UI touch still wins over a restore). HT-7 must record
  whether that same-pass UI-over-restore case is reachable in any tested
  host, since ADR-011 accepted it as a consequence rather than guaranteeing
  against it.
- **Gated, atomicity (new since ADR-011's acceptance):** the restored
  triple must land as one atomic publish, never observably partial. This is
  now a testable pass/fail criterion, not an open question: instrument
  `checkForNewTargets()`'s observed generation transitions across a restore
  and confirm exactly one transition covers all three coefficients, never a
  transition with only one or two of the three changed. This is the first
  HT-7 sub-case with a real gate rather than only a recorded observation.
- *Host-triggered restore context:* record whether restore occurs before or
  after `allocateRenderResourcesAndReturnError:` in each host, since ADR-010's
  lifecycle mapping and ADR-003 (d)'s "allocation only at `prepare`" contract
  interact differently in the two orders.
- *Determinism of the round trip as a round trip:* save, restore, render a
  deterministic script, and compare against the same script rendered from a
  directly-configured instance. ADR-011 decided the schema stores normalized
  control values that get re-derived on restore (not derived coefficients
  directly), which settles this comparison in favor of bit-identity rather
  than a tolerance-based comparison, since re-derivation through the same
  `publish()` path used elsewhere is exactly reproducible.

**What remains schema-dependent, and is therefore not designed here.**
Version and migration cases (an old-version state restored into a new
build, and its required behavior); what a "preset" means across a changed
delay set, which ADR-004's Consequences already list as open and ADR-008
explicitly declined to decide; the fixture-stamp mismatch surfacing
mechanism (ADR-011 decided "accept and surface," but the concrete surfacing
form is implementation-plan-level); `setAll`'s exact validation contract
(whole-triple reject vs. per-field fallback), which ADR-011 names as an
open implementation-plan-level detail; malformed/truncated/foreign-state
rejection behavior beyond what ADR-011 already specifies; and the size and
content of the state blob.

## HT-8 — dense or conflicting host automation/parameter events

Depends on [ADR-008](ADR-008-parameter-event-bridge.md).

**Configuration axes.** **Host app is the primary axis**: automation density,
event batching, and whether a host delivers `AUParameterEvent`s inside the
render callback at all are host properties. Block size: nominal plus a small
value, since events-per-callback scales inversely with block size and small
blocks are the stress case. Sample rate: one is sufficient; nothing in the
bridge is rate-derived. Device/chip tier: minor. OS version: minor.

**Pass/fail evidence shape.** Mostly recorded, with one gated bound.

- *Gated:* the render-thread work bound ADR-008 §3 specifies — per callback, at
  most three wait-free mailbox writes regardless of event count, no allocation,
  no lock. This is checkable by the same instrumentation HT-11 uses and should
  be recorded jointly with it.
- *Recorded:* the maximum number of `AUParameterEvent`s any tested host
  delivered in a single callback. ADR-008 explicitly did not choose a
  per-callback cap — it proposes one "as defense against a pathological or
  misbehaving host, but no specific cap value, or the behavior for events
  beyond it… is chosen here" — so HT-8's real contribution is the measurement
  that would let the implementation plan choose a cap on evidence rather than
  on a guess.
- *Recorded:* observed publication latency from event delivery to audible
  target change, against ADR-008 §5's estimate of "on the order of one to a few
  block periods (low single-digit milliseconds)," which that ADR labels "a
  reasoned estimate, not a proven bound." Any measurement here is the first
  evidence against that estimate and should be recorded as such, gated against
  nothing.
- *Recorded, and explicitly perceptual, not numeric:* whether ADR-008's accepted
  last-write-wins coalescing is audible under real host automation. ADR-008
  routes this to "testing.md's Sonic acceptance gate once real code and
  audition exist," not to a measurement — and this project already has a
  precedent for that routing, the DS-B Sonic acceptance rounds recorded in
  `docs/testing.md`. HT-8 should therefore produce the *renders* that gate would
  audition, not a coalescing pass/fail.

**What is explicitly NOT decided here.** The per-callback event cap or its
overflow behavior; the Controller's scheduling mechanism/priority (ADR-008
defers it to the implementation plan and calls it "the single biggest lever on
the online publication-latency estimate"); ADR-008's cross-producer tie-break
order; which hosts; how dense "dense" is.

## HT-9 — multiple concurrent instances

**Configuration axes.** **Device/chip tier is the primary axis**, and this is
the clearest case where it genuinely matters: how many instances a device
sustains is a function of its SoC and thermal envelope, not of the plugin's
logic. Sample rate: one is sufficient for the isolation half. Block size: one
nominal. Host app: one. OS version: minor.

**Pass/fail evidence shape.** Two separable things.

- *Gated, isolation:* two or more instances driven with different parameter
  settings and different input produce exactly the output each would produce
  alone. Bit-identity against single-instance reference renders is the right
  comparison (same binary, same configuration). This is the test that would
  actually discharge ADR-010's honestly-labelled source-inspection finding:
  ADR-010 inspected `src/dsp/*.h`/`*.cpp` directly and found "no mutable
  global, `static` (non-`constexpr`), `thread_local`, or singleton state
  exists anywhere in the portable core," then said plainly
  that this "supports, but does not prove, multi-instance safety… No test in
  this repository constructs and drives two instances of any of these classes
  concurrently." HT-9 converts an inspection into a measurement, and must be
  recorded in a way that makes clear which it is.
- *Recorded, not gated:* how many instances a named device sustains before
  audible dropout. This is a device-capacity observation with no defensible
  threshold until the owner states a product target, and this project's
  existing precedent for cost figures — DS-12's ≈67–69 ns/sample "reported as
  observation only," explicitly "not a CPU budget" — is the model.

**What is explicitly NOT decided here.** How many instances; which devices; what
constitutes "audible dropout" as an observation protocol; whether a product
instance-count target exists at all (it does not today).

## HT-10 — offline (non-realtime) render repeatability

**Configuration axes.** **Device/chip tier: no — explicitly.** This is the
clearest case in the catalog where the expensive axis buys nothing: offline
render repeatability is a determinism property of the same arithmetic on the
same binary, and running it on a second chip tier tests the compiler and the
floating-point environment, not the category's subject. If cross-tier
arithmetic identity is ever wanted, that is a *different* question (and a
harder one, since it is a claim about code generation) and should be named
separately rather than smuggled in as extra HT-10 coverage. OS version: no, for
the same reason. **Host app is the primary axis**, because "offline render"
means whatever each host's bounce/freeze/export path does — including whether
it renders at a different block size than realtime, whether it pre-rolls, and
whether it flushes the tail. Sample rate: both, cheaply. Block size: whatever
the host's offline path chooses, recorded rather than controlled.

**Pass/fail evidence shape.** Gated bit-identity, in the shape DS-11 already
established: the same host project bounced twice produces byte-identical audio
files, and the record states the host, its version, its offline settings, the
rate, and the observed block behavior. ADR-008 §5 makes a determinism claim
scoped precisely to this case and worth quoting as the thing HT-10 tests: "for
a fixed, given sequence of producer writes indexed by the block at which each
becomes visible to the Controller, a repeated offline render that drains
synchronously at the same points in the same order reproduces byte-identical
output on every run… It is not a claim that a live host session is reproducible
(no real-time system is)." HT-10 must therefore *not* be recorded as
"rendering is deterministic"; it is recorded as "this host's offline path
produced byte-identical output across N bounces with automation present /
absent," which is a narrower and true statement. A second, genuinely useful
sub-case: a bounce compared against this project's own offline renderer output
(the existing `tools/render_diffusion_stereo` / `tools/render_listening_batch`
family) — but only where the host's own pre-roll and tail handling are known,
otherwise the comparison measures the host, not the plugin, and should be
recorded as unmeasured rather than forced.

**What is explicitly NOT decided here.** Which hosts; how many bounces; whether
the cross-host or host-versus-own-renderer comparison is in scope at all;
tolerance policy if any host's path turns out not to be bit-reproducible for
reasons outside the plugin.

## HT-11 — realtime allocation/locking audit on the actual render callback, on-device

**Configuration axes.** Sample rate: both. Block size: nominal, plus zero and
plus a very small value, since the zero-frame early-return path and the
small-block path have different code coverage. Device/chip tier: minor — the
property under test is "does this code path allocate or lock," which is a
property of the code, not the chip; but see the honest caveat below about
*where* the audit runs. OS version: minor. Host app: one.

**Pass/fail evidence shape.** A gated zero-result, recorded with its
instrumentation method named. This project already has the on-host precedent
and it should be cited rather than reinvented: the DS-A record describes
"test-local regular, array, and aligned allocation overrides" recording "a
**zero** process/reset allocation delta after preallocation," and DS-12 records
"the global allocation counter's delta is 0 for every cascade." **The
substantive new difficulty HT-11 introduces is that those techniques are
link-time overrides inside this project's own test binary, and the AUv3 render
callback runs inside a host process this project does not build.** An honest
HT-11 design must therefore name which mechanism it uses on-device — a
`malloc` interposition, an Instruments/`os_signpost`-based audit, a
debug-build-only counter compiled into the extension, or a static audit of the
callback's transitive call graph — and record its limitations, because each of
these proves something slightly different and none of them proves the same
thing the host-side CTest override proves. Locking must be audited separately
from allocation (a lock-free-looking path can still block on a first-touch page
fault or an `os_unfair_lock` inside a system call), and any such finding is
recorded, not explained away.

**What is explicitly NOT decided here.** The instrumentation mechanism; whether
a debug-only counter is acceptable in the shipping extension's code (a real
design question, not a test detail); which devices; how long the audit runs.

## HT-12 — callback-deadline / worst-case timing measurement on named devices

**Configuration axes.** **Device/chip tier is the primary axis, and here it is
genuinely load-bearing** — unlike HT-10, this category measures nothing at all
without a named physical device, because the quantity being measured *is* a
property of that device's SoC, clock behavior and thermal state. Block size:
**several**, because the deadline is `frameCount / sampleRate` and the ratio of
work to deadline changes with block size; a plugin comfortable at 512 frames
can miss at 64. Sample rate: both, for the same reason (the deadline shrinks as
the rate rises). OS version: minor. Host app: one.

**Pass/fail evidence shape.** **Recorded, and ungated, until the owner sets a
budget.** This is the category most likely to be misreported, so its shape is
stated strictly. A record must contain: device model and chip; OS build; host
and version; sample rate; block size; the measured per-callback execution-time
distribution, including a maximum and high percentiles, not a mean alone — a
worst-case category reported as an average is not a worst-case measurement; the
number of callbacks sampled; the thermal condition and whether the device was
plugged in; whether other audio work was running; and the timing mechanism
used. It should also record the deadline itself alongside the measurement, so
the headroom ratio is visible rather than computed later from memory.

There is no pass threshold to apply. This project has been consistent about
this: DS-12's wall-clock figures are "reported as observation only," explicitly
"not a CPU budget," and ADR-004's own `D_max` precedent routes unresolvable
judgment calls to a listening/owner gate rather than to an invented number.
**A CPU budget is a product decision the owner has not made**, and HT-12 must
not manufacture one by picking a percentage. What HT-12 legitimately *can* gate
on, if the implementation plan wants a gate, is a failure that is not a
judgment call: a host-reported underrun or dropout attributable to this
plugin, at a recorded configuration.

**What is explicitly NOT decided here.** The CPU budget or headroom target; the
percentiles to report; the sampling duration; the thermal protocol; and which
devices — that last one is the subject of the next section.

## Device/OS scope policy — alternatives and recommendation

ADR-010 set the OS floor as a rolling policy and left the device side to the
owner. Its "Revisit when" line states the trigger precisely: **"the owner sets a
device/chip-tier scope."** That has not happened, so the question below is
posed as alternatives, not settled.

This is a **genuine owner/product/cost tradeoff, not an engineering fact.** Each
additional device tier and OS version multiplies device-lab cost, acquisition or
rental time, and re-run time on every future change. Each one omitted is
coverage the release does not have. No amount of reasoning from this
repository's contents determines where that line belongs.

Note the coupling ADR-010 already recorded: a given OS floor "implicitly admits
or excludes certain device/chip tiers," so the two axes are not independent —
choosing the oldest supported OS partly chooses the oldest plausible chip.

| Policy | Run matrix | Cost | Coverage confidence | Main risk it leaves |
|---|---|---|---|---|
| **(A) Corners only — recommended**: oldest supported OS on the oldest chip tier in scope, plus newest OS on the newest chip tier | 2 device/OS pairs | Lowest that is defensible | Covers the two ends of the performance and API-behavior range where HT-11/HT-12 failures concentrate | A middle-generation-only regression (an OS version that changed host behavior in the middle of the range) is invisible |
| **(B) Every OS major in the supported range × one representative device each** | Under ADR-010's "current minus one" floor this is currently 2 OS majors, so 2 pairs today — but the count grows automatically if the floor policy ever widens | Low today; grows silently with policy changes | Best OS-behavior coverage per unit cost while the range is two versions wide | Weak on the chip-tier axis: a single representative device hides thermal/deadline behavior at the low end |
| **(C) Full cross-product**: every supported OS major × every chip tier in scope | (OS majors) × (tiers) | Highest; grows multiplicatively and must be re-run on every change | Highest | Cost; and in practice, partial execution of a matrix that is then reported as if complete — the specific dishonesty this project's discipline exists to prevent |
| **(D) Corners plus a named tier floor for the timing categories only**: (A) for HT-1…HT-10, plus HT-11/HT-12 additionally on the weakest device tier the product intends to claim | 2 pairs, plus 1 device for the two timing categories | Slightly above (A) | (A)'s coverage, with the one axis that genuinely needs a low-end device explicitly covered there | Still no middle-OS coverage |

**Decision: (D) (owner-accepted, 2026-09-18)** — (A)'s two corners, extended
so that HT-11 and HT-12 additionally run on the weakest chip tier the product
intends to claim support for.

The reasoning, stated as reasoning and not as fact: the per-category axis
analysis above shows that most categories (HT-1, HT-2, HT-3, HT-5, HT-6, HT-7,
HT-8, HT-10, and the isolation half of HT-9) do not depend on chip tier at all
— they test logic, determinism, and lifecycle contracts that are properties of
this project's own arithmetic. Spending device-lab budget sweeping those across
tiers buys almost nothing. What is left is HT-12 and the capacity half of HT-9,
which measure a quantity that *is* a property of a named device, and HT-11,
which measures a property of the code but can only be observed inside a real
on-device render callback at all. For HT-12 and HT-9's capacity half the
*weakest* supported device is the informative one, because a timing result from
the newest M-series iPad says nothing about the oldest phone in scope, while
the converse is much closer to being true; HT-11 joins them on the weakest tier
for a narrower reason — the blocking it hunts for includes first-touch page
faults and memory-pressure-driven stalls, which a constrained device reaches
soonest. (D) therefore concentrates the expensive axis exactly where the
category analysis says it pays, and leaves it out where it does not.

(C) is not recommended, for a reason specific to this project rather than to
cost alone: a matrix too large to actually complete tends to get partially
executed and then summarized, which is precisely the "generic compatibility
claim" the evaluation plan forbids. A smaller matrix fully run and honestly
recorded is worth more here than a larger one partially run.

**This decision does not set the device/chip-tier scope**, which ADR-010
reserves to the owner and which remains open. (D) is a *shape* — "corners,
plus a low-end device for the timing categories" — that becomes a concrete
run matrix only once the owner names which tiers are in scope at all.

## Tooling policy — `auval`

**Proposed: `auval` is in scope as a cheap, first-line structural check, run
before and alongside HT-1…HT-12, and it substitutes for no HT category's
evidence.**

`auval` is Apple's own command-line Audio Unit validation tool, shipped with
macOS at zero additional cost. It exercises structural AU compliance —
component registration and discoverability, property getters/setters, bus and
format negotiation, render-with-various-configurations, and basic
instantiate/allocate/render/deallocate sequencing — and reports pass/fail per
check. Running it costs minutes and catches an entire class of packaging and
property-implementation mistakes before any device time is spent.

What it is **not**, stated explicitly so no future record blurs the line:

- `auval` validates the AU's *structure and property conformance*, not this
  project's *contracts*. It knows nothing about DS-B Task 3's cumulative fault
  counter surviving reset, about ADR-003 (d)'s transactional `prepare` rollback,
  about ADR-008's bounded render-thread work, about partition bit-identity, or
  about any figure in `docs/testing.md`.
- An `auval` pass is not HT-1 evidence, HT-2 evidence, or HT-3 evidence even
  where their subject matter overlaps, because `auval`'s own criteria are not
  this project's criteria and its output does not record the configuration
  detail this project's discipline requires.
- It runs on macOS. The AUv3 extension's on-device iOS/iPadOS behavior — which
  is what HT-11 and HT-12 exist to measure — is outside what it can tell us at
  all.

So the proposed policy is: run it, record its output (version, arguments, full
result) as its own clearly-labelled artifact, treat a failure as blocking
before device time is spent, and **never cite an `auval` pass as satisfying an
HT category.** A record that says "HT-1: `auval` passed" would be exactly the
kind of claim ADR-010 refused to make when it declined to treat its own
`src/dsp/` source inspection as multi-instance evidence.

## Decision and tradeoffs

This ADR proposes to fix: a per-category methodology for HT-1…HT-12 — the
configuration axes that actually matter for each, the shape a recorded result
must take to count as evidence, and what each category explicitly leaves to
execution time; the principle that the cheap axes (sample rate, block size) are
swept broadly while the expensive ones (device tier, OS version, host app) are
swept only where a category's failure mode depends on them; a device/OS scope
policy recommendation, (D), presented as one of four alternatives against a
real cost tradeoff; and `auval`'s scope as a first-line structural check that
substitutes for nothing.

It deliberately does not fix: which physical devices, which OS point-releases,
which hosts, how many trials, which tolerances, or any pass threshold for
HT-12 — all of which are implementation-plan or execution-time choices; the
device/chip-tier scope itself, which ADR-010 reserves to the owner; HT-7's
remaining schema-dependent half (versioning/migration cases and `setAll`'s
exact validation contract), which is now reconciled against ADR-011's
accepted content but still incomplete in those two respects; and every open
item ADR-008, ADR-009 and ADR-010 already named, none of which a test design
can close.

The main tradeoff inside this record is the one the axis tables encode: a
narrower, honestly-scoped matrix that is actually completable, versus a broader
one that would look like more coverage. This project's discipline already
answers that — "Missing measurements are **unmeasured**, never passes" — and
the catalog is written to make partial execution visible rather than
summarizable.

**Accepting this ADR would authorize no test code, no wrapper code, no CI, and
no device time.** The bounded wrapper implementation plan that ADR-007 requires
is still required, and HT-1…HT-12 remain unrun.

## Remaining decisions and later evidence

- **The device/chip-tier scope** — ADR-010's "the owner sets a device/chip-tier
  scope" is unchanged by this record. Which tiers are in scope remains an
  owner decision; which of alternatives (A)–(D) governs coverage is now
  decided as (D).
- **The concrete OS floor at execution time** — ADR-010's "current major
  version minus one" is a rolling policy, and its concrete iOS/iPadOS 25+ value
  reflects only the 2026-09-18 snapshot. Whoever schedules HT execution
  re-verifies it then, per ADR-010's own instruction.
- **HT-7's remaining schema-dependent methodology** — versioning/migration
  cases and `setAll`'s exact validation contract (whole-triple reject vs.
  per-field fallback) remain open; restore atomicity itself is now decided
  (ADR-011, alternative (iii)(B)) and HT-7's atomicity gate above reflects
  that. Malformed-state rejection and the round-trip comparison's
  tolerance/bit-identity policy are also now settled per ADR-011's
  acceptance and reflected above.
- **The wrapper's input-unavailable output policy (HT-4)** — silence, dry
  passthrough, or continuing wet tail when an input pull fails. No accepted ADR
  decides this. It is adjacent to ADR-009's bypass reasoning but is not covered
  by it, and HT-4 can only record what gets built, not validate a policy that
  does not exist. Flagged here as a gap in the decision record, not assigned.
- **HT-5's pass criteria branch on ADR-009's open items** — the bypass
  CPU-versus-tail-continuity choice, the optional "kill tail" control, and
  `canProcessInPlace`'s value are all left open by ADR-009 and must be closed by
  the implementation plan before HT-5's criteria can be written down.
- **Whether HT-12 ever gets a numeric gate** — a CPU budget or headroom target
  is a product decision the owner has not made. Until then HT-12 is a
  measurement category, and this ADR recommends it stay one rather than acquire
  an invented threshold.
- **Whether cross-device arithmetic identity is ever in scope** — named under
  HT-10 as a *different* question from offline repeatability, deliberately not
  folded into it. If the product ever needs it, it needs its own category and
  its own argument, not extra HT-10 runs.
- **Whether HT-11's on-device instrumentation may compile anything into the
  shipping extension** — a real design question the implementation plan must
  answer, since the existing host-side allocation-override technique does not
  transfer to a host process this project does not build.
- **No evidence exists for any of the above.** Every figure cited in this record
  (`{1,13,64,512,3}`, `{7,29,3,211,5}`, `{1,13,64,512,977}`, 321,914 / 297,234
  samples, ≈67–69 ns/sample, allocation delta 0) is prior host-side measurement
  on macOS arm64, quoted as context for designing a test, never as a device or
  host result.

## Revisit when

the implementation plan resolves `setAll`'s exact validation contract and
the versioning/migration cases ADR-011 left open, at which point HT-7's
remaining gaps close; the owner sets the device/chip-tier scope, at which
point decision (D) above becomes a concrete run matrix; the owner sets a CPU
budget or headroom target, at which point HT-12 stops being purely a
measurement category; ADR-009's bypass open items (wet-path-during-bypass,
"kill tail" control, `canProcessInPlace`) are closed by a bounded
implementation plan, at which point HT-5's pass criteria can be fixed;
ADR-008's per-callback event cap or Controller scheduling mechanism is chosen,
changing what HT-8 must record; the cross-thread `reset()`-versus-render
question ADR-010 left unassigned is adopted by some record, changing HT-6 part
3 from an instrumentation observation into a contract with a real pass
criterion; a wrapper implementation plan is authorized and HT-1…HT-12 begin
producing real device/host evidence that contradicts an assumption recorded
here — particularly ADR-010's block-partition generalization inference, which
HT-3 exists to replace, and ADR-010's source-inspection multi-instance finding,
which HT-9 exists to replace; a host is observed requesting a block size
outside the measured partitions, making HT-3 actionable rather than named (the
same trigger ADR-010 already records); or ADR-010's rolling deployment floor
advances far enough that the OS range widens beyond two majors, at which point
alternative (B)'s cost stops being equal to (A)'s and the scope policy needs
re-deciding rather than re-reading.
