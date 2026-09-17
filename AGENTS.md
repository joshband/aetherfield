# Working in Aetherfield

Start with [docs/start-here.md](docs/start-here.md). It routes humans and agents
to current status, the active plan, relevant ADRs and evidence.

Read the active task's plan and referenced decision sections before editing.
Use [the ADR index](docs/decisions/index.md) to select records; do not load the
entire decision archive or every historical plan by default. Read dependencies
when they govern the behavior being changed, not merely because they exist.

The roadmap defines milestone scope; an accepted ADR does not itself authorize
implementation. Current source and recorded verification establish what exists.
Preserve unrelated working-tree changes. Keep summaries linked to authoritative
records, and distinguish implemented, planned and deferred work.

## Lean agent loop

1. Start a fresh session or clear prior context at a milestone boundary.
2. Read `docs/start-here.md`, then the active plan, then only the ADR sections
   and testing sections named by that plan. Use `rg -n` plus narrow `sed` ranges;
   do not dump whole archives into context.
3. Route work by consequence: Luna for mechanical verification, Terra for
   bounded implementation, Sol/high effort for architecture or numerical safety,
   and Astra for orchestration and scope decisions. Record the actual tool/model
   in the milestone handoff, never infer it later.
4. Keep implementation, verification and documentation in separate checkpoints.
   Each checkpoint records commands, exit status, changed files, known gaps and
   the next action in the relevant plan or `docs/agent-log.md`.
5. Before switching tools, leave a compact handoff in `docs/start-here.md` or
   the active plan. A new agent should be able to resume from disk with one
   focused prompt; do not depend on chat history.

Suggested resume prompt:

> Read `docs/start-here.md`, then the named active plan. Inspect only the
> referenced source/tests. Continue the next unchecked task, preserve dirty
> files, and report exact verification commands and remaining gaps.
