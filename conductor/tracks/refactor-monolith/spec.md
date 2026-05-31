# Spec: Refactor vcfdist Monolithic Codebase

## Summary

Coordinate safe, measured performance-refactor slices for vcfdist's existing C++17 monolithic source tree. Each slice must preserve current correctness and public behavior unless a separate approved proposal explicitly changes semantics.

## Where the spec details live

Goals, requirements, classification taxonomy (pure refactor / performance refactor / optimization / port/rewrite), correctness acceptance, performance acceptance (including the `3.0x` per-core RSS cap), and non-goals are defined once in the docs below. Do not restate them here.

| Topic | See |
|---|---|
| Goals and non-goals | `docs/refactoring-plan.md § Goals`, `§ Non-goals` |
| Slice classification | `docs/refactoring-plan.md § Slice classes` |
| Correctness acceptance | `docs/refactoring-plan.md § Correctness gate`, `testing.md § Correctness regression gate` |
| Performance acceptance, RSS cap, thread sweep | `docs/refactoring-plan.md § Performance gate`, `testing.md § Baseline`, `testing.md § Timed-output validation` |
| Design approval rules | `docs/refactoring-plan.md § Workflow per slice` step 7, `docs/agents/index.md § Loop boundaries` |
| Implementation conventions | `docs/coding-guidelines.md` |
| Hotspots and structural tensions | `docs/architecture.md § Known structural tensions` |
| Tried and retired directions | `docs/refactoring-plan.md § Tried and retired` |
| Live measurements | `docs/benchmark-progress.json` |

## Track-specific conventions

- Branch naming: `refactor/<short-slice-name>` for pure/performance refactors, `perf/<short-slice-name>` for optimizations, `port/<short-slice-name>` for approved port experiments.
- Branches start from `master`.
- Commits compile clean under `-Wall -Wextra`.
