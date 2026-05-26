# Spec: Refactor vcfdist Monolithic Codebase

## Summary

Coordinate safe, measured performance-refactor slices for vcfdist's existing C++17 monolithic source tree. Each slice must preserve current correctness and public behavior unless a separate approved proposal explicitly changes semantics.

## Goals

- Improve high-core scaling efficiency, especially at the 64-core target.
- Work toward an eventual 2x wall-clock speedup on the canonical chr21 benchmark.
- Preserve output files, precision/recall behavior, genotyping error rates, and switch error rates.
- Keep per-core memory growth below the threshold in `docs/refactoring-plan.md` unless explicitly approved.
- Use benchmark evidence and architecture notes to select slices, not file size alone.
- Keep changes small enough to review, benchmark, and revert.
- Maintain `docs/benchmark-progress.json` as the live measurement index.
- Update `docs/architecture.md` when implementation changes alter file structure, data flow, concurrency, or language/runtime boundaries.

## Requirements

### Slice selection

- Explorer must identify candidate slices from source structure, benchmark evidence, and known bottlenecks.
- Each candidate must name the touched files/symbols, suspected hot path or scaling bottleneck, expected speedup mechanism, memory-risk profile, and blast radius.
- Oracle must pick at most one candidate per cycle and classify it as one of:
  - pure refactor;
  - performance refactor;
  - optimization;
  - port/rewrite experiment.

### Design approval

- A design note is required before implementation.
- The design note must include target seams, performance hypothesis, correctness risk, memory-risk assessment, benchmark plan, and expected output diff (`none`).
- Explicit approval is required before changing algorithms, threading, memory layout, runtime/language, dependencies, CLI, output semantics, or public reports.

### Implementation

- Work branches start from `master`.
- Branch names follow `refactor/<short-slice-name>`, `perf/<short-slice-name>`, or `port/<short-slice-name>`.
- Commits should be small and each commit should compile clean under `-Wall -Wextra`.
- New C++ code must follow `docs/coding-guidelines.md`.
- Behavior preservation per commit is the default unless the approved design allows a non-final intermediate.

### Correctness acceptance

- Run the canonical `fixtures/hg03784_chr21_grch38/` fixture with the Docker command in `testing.md`.
- Compare branch outputs to the archived `v2.6.4` baseline output tree.
- Output files must match byte-for-byte or numerically under existing project tolerances where byte identity is not expected.
- Genotyping error and switch error rates must not change.
- Any stdout/stderr differences must be explained in the verification note.

### Performance acceptance

- Record canonical measurements in Docker unless the design explicitly states otherwise.
- Record wall-clock runtime, maximum resident set size, thread count, image/digest, host CPU/NUMA notes, baseline tag/commit, branch commit, and output-diff verdict.
- Preferred thread sweep: `1, 8, 16, 32, 64`.
- Optional stretch sweep: `128, 256`.
- Pure refactors must not materially slow the canonical benchmark.
- Performance refactors may be accepted as enabling-only if the design explains the later optimization they enable and why any intermediate cost is acceptable.
- Optimizations should show measured improvement in wall-clock runtime or scaling efficiency.
- Memory at equal thread counts must remain below `1.3x` baseline peak RSS unless explicitly approved.

## Non-goals

- Do not trade correctness for speed.
- Do not change public CLI, output formats, or report semantics as part of routine performance work.
- Do not rename existing symbols merely to match a new style.
- Do not add design-pattern abstractions unless they enable a measured performance change, delete more code than they add, or structurally prevent a recurring bug class.
- Do not add new dependencies, language runtimes, generated build systems, or build complexity without explicit approval.

## Initial hotspots and structural tensions

Current architecture notes identify these areas as likely investigation targets, pending benchmark evidence:

- `dist.cpp` — largest source file; contains precision/recall, edit-distance summarization, BiWFA distance routines, wave/queue helpers, and current threading driver.
- `cluster.cpp` — clustering, superclustering, and BiWFA-driven realignment path.
- `variant.cpp` — VCF read/write and per-contig/top-level variant containers.
- `print.cpp` — all report writers and INFO formatting.
- `globals.cpp` — mostly CLI parsing; hidden churn risk through the global `Globals` singleton.

Current parallelism is per-supercluster inside `dist.cpp::precision_recall_threads_wrapper`. Any change that crosses thread boundaries must preserve disjoint output ownership or justify synchronization.

## Reference documents

- `AGENTS.md`
- `docs/refactoring-plan.md`
- `docs/architecture.md`
- `docs/coding-guidelines.md`
- `docs/multiagent-process.md`
- `docs/benchmark-progress.json`
- `testing.md`
- `INFRASTRUCTURE_SUMMARY.md`
