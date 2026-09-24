# HT automation limits — what a REAPER render can and cannot settle

**Date:** 2026-09-24
**Scope:** the macOS/REAPER HT subset (HT-4, 5, 6, 7, 8, 9)
**Companion:** `scripts/ht_reaper/` (the harness), `HT_macOS_EXECUTION_PLAN.md` (the manual procedures)

This record exists because `HT_macOS_EXECUTION_PLAN.md` lists six tests as
"ready to execute on macOS/REAPER" without distinguishing *which of their gates a
rendered audio file can actually decide*. Three cannot be decided that way. Automating
them against REAPER anyway would produce `manifest.json` files that look like
evidence and are not — the exact conflation this project's own records warn
against ("do not treat measured, compared or accepted as authorized"; "when these
disagree, do not silently reconcile them").

## Automated — gate decidable from rendered audio

| Test | Gate | How the harness decides it |
|---|---|---|
| HT-5 part 1 | bit-exact dry passthrough under host bypass | render with/without the AU, compare WAV `data` payloads |
| HT-5 part 2 | no hard discontinuity on un-bypass | square bypass envelope, measure max sample-to-sample delta across each transition |
| HT-6 silence half | reset clears state | differential render: a window containing no input must be silent, with a control render proving a tail existed to clear |
| HT-7 round-trip half | save → reopen → render is bit-identical | `Main_SaveProjectEx` (drives `getState`) then reopen (drives `setState`), compare payloads |
| HT-9 isolation half | concurrent instances don't influence each other | stem renders of two live instances vs. the same instances rendered alone |

## Not automated — gate is not decidable from rendered audio

### HT-4 — input-pull failure / host underrun

ADR-012's gate is that the AU survives `pullInputBlock` **returning an error**.
`HT_macOS_EXECUTION_PLAN.md` §HT-4 proposes simulating this by record-arming a
track with no input selected. That does not produce a pull failure: REAPER
supplies a valid, silent buffer and `pullInputBlock` returns `noErr`. The test
would pass while never exercising the error path it names.

A REAPER project cannot make a host return an error status to a plugin. Deciding
this gate needs a harness that supplies its own `pullInputBlock` and returns a
failing `OSStatus` — which the existing `AetherfieldHarnessTests` XCTest target
is already the natural home for.

Related and worth carrying into any such harness: this project has already
confirmed, and cross-checked against two third-party AUv3 extensions, that
Apple's out-of-process render proxy **silently skips `pullInputBlock` entirely**
on the call following a zero-frame render request. Any HT-4 harness will meet
that behaviour and should distinguish it from a genuine AU fault.

### HT-6 — the other half of the dual gate

Two of HT-6's requirements are outside REAPER's reach:

- **"cumulative fault counter unchanged across reset."** The counter is internal
  wrapper state with no audio-visible consequence and no host-readable property.
  Nothing in a WAV file can show it.
- **"self-triggered recovery on non-finite input."** §HT-6 Part 2 proposes a
  REAPER gain envelope "with one point set to `inf`". REAPER envelopes are
  bounded by the parameter's own range and cannot carry `inf` or `NaN`; the
  envelope simply clamps. There is no route from the REAPER UI to a non-finite
  sample at the AU's input.

The harness therefore renders and decides only the silence half, and records both
of these explicitly as `notCovered` in the manifest rather than omitting them.

### HT-8 — dense/conflicting automation

ADR-012's gate is **≤3 wait-free mailbox writes, 0 allocations, 0 locks per render
callback**. §HT-8 Part 1 proposes reading REAPER's CPU meter. A CPU meter cannot
observe an allocation, a lock, or a mailbox write; it reports aggregate time.
A render that sounds correct and costs little CPU is consistent with both
satisfying and violating this gate.

Deciding it needs render-thread instrumentation — an allocation hook and a lock
counter around the parameter-bridge drain, asserted per callback. That is
implementation work against `src/wrapper/`, not test execution.

The *recorded* (non-gated) half of HT-8 — renders of dense automation envelopes
for coalescing audibility — is automatable and could be added, but it does not
close the gate and is not included here.

### HT-11, HT-12

Unchanged from `HT_STATUS_SUMMARY.md`: both require a physical iOS device with
on-device instrumentation.

## Consequence for HT status

Automating the harness closes **HT-5, HT-7 and HT-9 fully**, and **HT-6 partially**
(one of two gate halves). It closes **none of HT-4 or HT-8**.

Reaching a complete HT-4…HT-9 set requires an instrumented offline harness
(failing-`pullInputBlock` injection, non-finite input injection, allocation/lock
counters). That is new implementation and is **not authorized** by this document,
by the execution plan, or by the harness. It is recorded here as the named next
decision, per this project's standing separation of measurement from
authorization.
