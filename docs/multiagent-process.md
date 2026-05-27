# Multiagent Process

The agent roles, phases, and information flow are defined once in [`./agents/index.md`](./agents/index.md) (`oh-my-opencode-slim` taxonomy). This file does not restate them. It records the orchestrator-facing notes that complement that map.

## Where each artifact lives

| Phase | Producer | Artifact | Repo location |
|---|---|---|---|
| Discovery | `explorer` | Candidate-slice note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Design | `designer` (with `oracle` risk review) | Slice design note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Implementation | `fixer` | Refactor/perf/port branch + PR | `refactor/<slice-name>`, `perf/<slice-name>`, or `port/<slice-name>` branch, PR against `master` |
| Verification | `fixer` (gates) + `documentarian` (doc drift) | Gate verdict + doc-alignment note | PR comment; `docs/benchmark-progress.json` updated when measurements are produced |

The `docs/dev/` directory is created on demand the first time a merged note is committed; it does not exist preemptively.

## Loop boundaries

- **`explorer` → `designer`** is the only handoff that can produce multiple candidate slices. `designer` (with `oracle`'s risk review) picks at most one per cycle.
- **`fixer` ↔ gate verdict** is *not* a tight retry loop. A failing gate returns control to **`designer`/`oracle`**, not `fixer`. The premise is that a failing gate means the slice was the wrong shape, and reshaping is a Design responsibility.
- **Cross-slice coordination** (two refactor branches that touch the same file) is the orchestrator's responsibility. If two slices conflict, the second slice replans against the merged state of the first.
- **Approval before implementation** is mandatory. Design notes require approval before `fixer` starts. Algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes require explicit approval in the design note.
- **Completion after approval** is the default. Once a design is approved, `fixer` runs the full implementation/verification loop (including the correctness and performance gates) to completion unless a gate fails or the design assumptions are proven wrong.

## Gates

Defined in `docs/refactoring-plan.md` under "Correctness gate" and "Performance gate". `fixer` invokes them and reports the verdict; no role can waive them. Changes to a gate are separate document updates, not per-slice decisions.

Correctness gates protect output equivalence and the measured genotyping/switch/flip error rates. Performance gates protect wall-clock runtime, scaling efficiency, peak RSS/per-core memory behavior, and the output equivalence of the exact timed benchmark output tree.

## Benchmark tracker

`docs/benchmark-progress.json` is the machine-readable live record of benchmark progress. It records the canonical baseline, target hardware assumptions, benchmark fixtures, measured slice results, output-validation policy, and open measurement gaps.

The tracker is not a substitute for archived raw outputs. It is the index that lets the next `explorer`/`designer`/`oracle` cycle choose work from evidence instead of memory.

When a slice produces benchmark data, update the tracker in the same branch as the implementation or verification note. Enabling-only slices should still record that no speedup claim was made and name the later measurement they enable.

## Companion documents

See [`../AGENTS.md`](../AGENTS.md) for the agent-facing TOC and [`./agents/index.md`](./agents/index.md) for the delegation map.
