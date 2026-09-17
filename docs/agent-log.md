# Agent activity log

A consolidated, per-milestone index of which coding tool, agent role, model
and reasoning effort executed each task, plus whatever quantitative metrics
the executing harness actually reported. This is a **derived convenience
index, not a new source of truth**: architecture.md's principle stands —
tool/model choices are an execution detail that belongs primarily in that
session's own commit message. Every row below is sourced from an existing
commit message or a metric the harness reported directly in-session; nothing
here is estimated or invented.

Per testing.md's evidentiary standard applied to this log: **a blank cell
means the value was never recorded or measured, not zero and not
"negligible."** Token/duration/tool-call counts are only as complete as the
tool in use chooses to report them; several rows below predate any metrics
being requested at all.

| Date | Task | Tool | Role(s) and model(s) | Effort | Measured metrics | Source |
|---|---|---|---|---|---|---|
| (unrecorded) | Bootstrap Aetherfield portable DSP engineering loop (Phase 0) | Unspecified — commit carries no role/model attribution or `Co-Authored-By` trailer | Unspecified | Unspecified | None recorded | `9051b67` |
| 2026-09-16 | Resolve Xcode license blocker; record Phase 1 validation gates | Claude Code | Luna (model tier not stated in commit) | Not stated | None recorded | `43db7a9` |
| 2026-09-16 | Phase 1: decide reverb topology (ADR-002); plan S1 skeleton | Claude Code (switched from the prior tool's stack per this commit's own note) | Sol: Opus · Terra: Sonnet · Luna: Haiku | Sol: highest (architectural/safety); Terra: standard; Luna: light/mechanical | None recorded | `c4cd61a` |
| 2026-09-16 | Phase 1: damping, denormal handling, full-network lifecycle (ADR-003) | Claude Code | Sol: Opus · Luna: Haiku | Sol: highest; Luna: light/mechanical | None recorded | `f852f79` |
| 2026-09-16 | Document agent roles as tool-agnostic, with per-role effort guidance | Claude Code | Astra (documentation only; no Sol/Terra/Luna task split) | — | None recorded | `072fea9` |
| 2026-09-16 | Phase 1: initial parameter surface and automation boundary (ADR-004) | Claude Code | Sol: Opus · Luna: Haiku | Sol: highest; Luna: light/mechanical | None recorded | `27f8e75` |
| 2026-09-16 | Phase 1: consolidate NS/PT cases into one verification-case spec | Claude Code | Terra: Sonnet · Luna: Haiku | Terra: standard; Luna: light/mechanical | None recorded | `8ecf15b` |
| 2026-09-16 | Mark Phase 1 exit criteria as met; implementation still separately authorized | Claude Code | Astra (scope/status only) | — | None recorded | `adba640` |
| 2026-09-16 | Phase 1 S1: implement `DelayLine` (delay/lifecycle skeleton) + doc sync | Claude Code | Terra: Sonnet 5, standard effort, in-session (this session's main agent) · Luna: Haiku, dispatched as an independent background subagent | Terra: standard; Luna: light/mechanical, explicitly re-derives evidence from disk rather than trusting Terra's self-report | Luna subagent (harness-reported): **43,571 tokens, 14 tool calls, 83.4s wall time**. Terra/main-session cost was not separately measurable — the harness does not expose a mid-session token/time counter for the orchestrating agent itself. | This commit |

## On "Codex"

Commit `c4cd61a` records that the project "has in practice been continued
across more than one tool" and that this project's Astra/Sol/Terra/Luna
roster was, at that point, switched onto Claude models "in place of the
prior tool's stack, to spread usage across both tools." Neither that commit
nor any earlier one names the prior tool explicitly in tracked history; it is
referenced here only because it is the source of that claim, not because
this log can independently confirm which tool produced the bootstrap commit.
Every row above with tool "Claude Code" reflects this repository having been
worked on inside Claude Code specifically (not the Claude API or another
harness) for that task.

## Maintenance

Add a row here at the same time a milestone-level commit lands, sourcing it
from that commit's own body (which remains the primary record) plus any
metrics the harness reported during that session. Do not backfill a metric
that was not actually measured just to avoid a blank cell.
