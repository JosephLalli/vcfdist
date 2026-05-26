# Multiagent Process

The agent roles, phases, and information flow are defined once in [`../AGENTS.md`](../AGENTS.md). This file does not restate them. It records the orchestrator-facing notes that complement that brief.

## Where each artifact lives

| Phase | Producer | Artifact | Repo location |
|---|---|---|---|
| Discovery | Explorer | Candidate-slice note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Design | Oracle | Slice design note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Implementation | Fixer | Refactor/perf/port branch + PR | `refactor/<slice-name>`, `perf/<slice-name>`, or `port/<slice-name>` branch, PR against `master` |
| Verification | Librarian | Correctness/performance gate verdict | PR comment; `docs/benchmark-progress.json` updated when measurements are produced |

The `docs/dev/` directory is created on demand the first time a merged note is committed; it does not exist preemptively.

## Loop boundaries

- **Explorer → Oracle** is the only handoff that can produce multiple candidate slices. Oracle picks at most one per cycle.
- **Fixer ↔ Librarian** is *not* a tight retry loop. A failing gate returns control to **Oracle**, not Fixer. The premise is that a failing gate means the slice was the wrong shape, and reshaping is a Design responsibility.
- **Cross-slice coordination** (two refactor branches that touch the same file) is the orchestrator's responsibility. If two slices conflict, the second slice replans against the merged state of the first.
- **Approval before implementation** is mandatory. Oracle design notes require approval before Fixer starts. Algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes require explicit approval in the design note.
- **Completion after approval** is the default. Once a design is approved, Fixer and Librarian run the full implementation/verification loop to completion unless a gate fails or the design assumptions are proven wrong.

## Gates

Defined in `docs/refactoring-plan.md` under "Correctness gate" and "Performance gate". The Librarian role enforces them; no other role can waive them. Changes to a gate are separate document updates, not per-slice decisions.

Correctness gates protect output equivalence and the measured genotyping/switch error rates. Performance gates protect wall-clock runtime, scaling efficiency, and peak RSS/per-core memory behavior.

## Benchmark tracker

`docs/benchmark-progress.json` is the machine-readable live record of benchmark progress. It records the canonical baseline, target hardware assumptions, benchmark fixtures, measured slice results, and open measurement gaps.

The tracker is not a substitute for archived raw outputs. It is the index that lets the next Explorer/Oracle cycle choose work from evidence instead of memory.

When a slice produces benchmark data, update the tracker in the same branch as the implementation or verification note. Enabling-only slices should still record that no speedup claim was made and name the later measurement they enable.

## Companion documents

See the bottom of [`../AGENTS.md`](../AGENTS.md) for the full set.
