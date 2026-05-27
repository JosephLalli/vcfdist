# AGENTS.md

## Purpose

This file is the agent-facing entry point for vcfdist work. It is not the broad repository index and not a detailed role manual. Detailed role definitions live in [`docs/agents/`](docs/agents/); the broad repo map lives in [`INFRASTRUCTURE_SUMMARY.md`](INFRASTRUCTURE_SUMMARY.md).

## Read first

| Need | Read |
|---|---|
| Broad repo map | `INFRASTRUCTURE_SUMMARY.md` |
| Agent delegation | `docs/agents/index.md` |
| Architecture | `docs/architecture.md` |
| Coding conventions | `docs/coding-guidelines.md` |
| Testing / verification | `testing.md` |
| Refactoring policy | `docs/refactoring-plan.md` |
| Benchmark tracker | `docs/benchmark-progress.json` |

## Hard constraints

- Do not change benchmark thresholds without `oracle` or human review.
- Do not broaden a scoped implementation task without returning to `orchestrator`.
- Do not duplicate canonical policy text; link to the canonical source.
- Do not commit per-harness duplicate instruction files (`COPILOT_INSTRUCTIONS.md`, `GEMINI*.md`, `.cursorrules`, etc. — see `.gitignore`).
- Do not put content in `CLAUDE.md`; it is a pointer file. Edit `AGENTS.md` or the relevant doc instead.
- Do not add AI-attribution markers to code, docs, or commit messages.
- Keep `AGENTS.md` short; move detail into `docs/`.
- Keep `INFRASTRUCTURE_SUMMARY.md` as the broad index.

## `oh-my-opencode-slim` delegation

Use `docs/agents/index.md` as the delegation map. Short routing summary:

- `orchestrator`: task decomposition, delegation, reconciliation.
- `explorer`: repository reconnaissance.
- `librarian`: external docs and current source-grounded facts.
- `oracle`: architecture, policy, risk, benchmark interpretation.
- `designer`: interfaces, acceptance criteria, test design.
- `fixer`: bounded implementation and gate execution.
- `council`: expensive multi-model judgment.
- `observer`: visual/PDF/image artifacts, if enabled.
- `documentarian`: documentation drift and canonical-source consistency.

Delegation rules live in `docs/agents/index.md`; detailed role behavior lives in `docs/agents/*.md`.

## Workflow expectations

1. `orchestrator` scopes the task.
2. `explorer` and/or `librarian` gather context when needed.
3. `designer` and/or `oracle` produce design or risk review for non-trivial changes.
4. `fixer` implements only bounded tasks and invokes the correctness/performance gates.
5. `documentarian` checks doc drift when behavior, tests, architecture, or agent docs change.
6. `oracle` or human review is required for benchmark-policy or architecture-policy changes.

A failing correctness or performance gate returns control to `designer`/`oracle`, not to `fixer`. A failing gate means the slice was the wrong shape, not just the wrong commit.

## Verification

Run the agent-doc drift gate before declaring doc-touching work done:

```bash
bash tools/check_agent_docs.sh
```

Then run the task-specific gate from `testing.md` (e.g., `bash demo/regression.sh` for correctness, `/usr/bin/time -v src/vcfdist ...` for performance).

## What this brief is not

- Not an orchestrator config. The agents are configured externally (Conductor / OpenCode); this repo does not host their runtime.
- Not a status-report cadence. Reporting is the orchestrator's concern.
- Not a project plan. Project planning lives in `docs/refactoring-plan.md`.
- Not a coding style guide. That lives in `docs/coding-guidelines.md`.
