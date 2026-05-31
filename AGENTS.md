# AGENTS.md

Table of contents for agents working on vcfdist. This file holds pointers, not content; the documents pointed to are canonical.

## Where to look

| For | See |
|---|---|
| Broad repo map and tooling | `INFRASTRUCTURE_SUMMARY.md` |
| Agent delegation, agent-process rules, loop boundaries, where artifacts live, hard constraints | `docs/agents/index.md` |
| Per-role definitions (Codex harness) | `docs/agents/*.md` |
| Architecture (`src/` modules, data flow, structural tensions) | `docs/architecture.md` |
| Coding conventions and correctness/performance invariants | `docs/coding-guidelines.md` |
| Refactor goals, gates, per-slice workflow, tried-and-retired | `docs/refactoring-plan.md` |
| Testing protocol, fixtures, benchmark command, validation policy | `testing.md` |
| Live benchmark tracker | `docs/benchmark-progress.json` |
| Cross-harness project status (other harness reads this) | `conductor/` |
| User-facing wiki mirror (not internal tracking) | `docs/dev/` |

## Cross-harness notes

- This repository is worked on by multiple agent harnesses. Documentation under `docs/` is harness-agnostic.
- `conductor/` is the other harness's project-status hub. Treat it as a peer to `docs/`, not as a duplicate.
- `docs/agents/*.md` defines roles for the Codex harness. The Claude harness uses agents from `~/.claude/agents/` with equivalent role semantics; Claude agents may ignore the files under `docs/agents/` as long as the role intent matches.
- `CLAUDE.md` is a one-line pointer; do not put content there. Put Claude-specific behavior in `~/.claude/` settings or memory, not in the repo.

## Verification

Before declaring doc-touching work done:

```bash
bash tools/check_agent_docs.sh
```

Then run the task-specific gate from `testing.md` (`bash demo/regression.sh` for correctness, `/usr/bin/time -v src/vcfdist …` for performance).
