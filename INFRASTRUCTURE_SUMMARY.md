# Repository Doc Map

The canonical index of every internal-facing document in this repository. `AGENTS.md` is a brief table of contents that points here; this file holds the broad map.

## Read first

| Need | Read |
|---|---|
| Broad repo map | this file |
| Agent delegation, agent-process rules, loop boundaries, where artifacts live | `docs/agents/index.md` |
| Per-role agent definitions (Codex harness) | `docs/agents/*.md` |
| Architecture (`src/` modules, data flow, file sizes, structural tensions) | `docs/architecture.md` |
| Coding conventions and correctness/performance invariants | `docs/coding-guidelines.md` |
| Refactor goals, gates, per-slice workflow, tried-and-retired | `docs/refactoring-plan.md` |
| Testing protocol, fixtures, benchmark command, validation policy | `testing.md` |
| Live benchmark tracker (machine-readable) | `docs/benchmark-progress.json` |

## Cross-harness project state

| Location | What it holds |
|---|---|
| `conductor/` | Cross-harness project status, tracks, and per-track plans. Source of truth that the other harness reads for "what's been tried, what the plan is, what we ruled out." |
| `docs/refactoring-plan.md § Tried and retired` | Permanent record of design directions ruled out, with the evidence that killed them. |
| `docs/benchmark-progress.json` | Live benchmark measurements per slice. |

## User-facing docs (not internal tracking)

| Location | Purpose |
|---|---|
| `README.md` | Public install / quickstart / citation. |
| `docs/dev/` | Mirror of the GitHub wiki. End-user reference. Not a place for internal project tracking or design notes. |
| `fixtures/*/README.md` | Per-fixture purpose blurb. Run commands and protocol live in `testing.md`. |

## Tooling

| Path | Purpose |
|---|---|
| `tools/compare_vcfdist_runs.py` | Compare two `vcfdist` output trees and report runtime/RSS deltas with headline accuracy metrics. |
| `tools/check_agent_docs.sh` | Mechanical drift checker for the agent-doc topology and the no-duplicate-policy rules. |
| `demo/regression.sh` | Automated chr1 5 Mb correctness regression gate against checked-in `demo/results/`. |
| `demo/demo.sh` | Original chr1 5 Mb demo/plot script for manual visual refreshes. |

## Refactor-engineer reading order

Starting from cold: `AGENTS.md` → this file → `docs/refactoring-plan.md` → `docs/benchmark-progress.json` → `testing.md` → `docs/architecture.md` → `docs/coding-guidelines.md` → start an approved slice branch.
