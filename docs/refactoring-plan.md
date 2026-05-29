# vcfdist Performance Refactoring Plan

## Why this exists

vcfdist is correct, useful, and slow. This plan exists to make performance work safe enough to run repeatedly on a brownfield production tool.

The primary objective is to reduce wall-clock runtime in high-core environments while preserving output correctness and keeping per-core memory usage bounded. Efficient use of 64 cores is the practical target. NUMA-aware scaling toward 256 cores is a stretch goal, not the first acceptance bar.

Refactoring is justified when it enables measured speedup, removes a performance bottleneck, lowers the risk of a performance change, or preserves correctness while isolating a hot path for later work. Readability-only cleanup is secondary.

## Slice classes

Each work item is classified before implementation:

1. **Pure refactor.** Code moves or decomposition only. Expected output unchanged; expected speed neutral. Must not slow the benchmark materially.
2. **Performance refactor.** Structure changes that prepare or expose a speedup, such as isolating a kernel, scheduler, or data layout. May be enabling-only if the design note says so explicitly.
3. **Optimization.** Algorithm, scheduling, allocation, data-layout, or I/O change intended to improve runtime or scaling efficiency.
4. **Port/rewrite experiment.** Rust or another implementation language is introduced for an isolated path, prototype, or production slice. This is allowed only after explicit design approval.

The default path is C++ performance refactoring. Rust is on the table both as an experiment and, after approval, as production implementation. It is not assumed for every slice.

## Correctness gate (mandatory)

No branch merges unless output correctness is unchanged against the authoritative demo regression baseline.

Correctness diffs use the checked-in `demo/results/` baseline. Development branches start from `master`.

Required correctness checks:

1. **Demo regression gate.** Build `src/vcfdist`, then run `bash demo/regression.sh`. The generated output files must match the checked-in `demo/results/` baseline under the comparison rules in `testing.md`. The measured genotyping, switch, and flip error counts must not change.

Smaller ad hoc fixtures may be used for compile, smoke, and intermediate sanity checks. They do not replace the demo regression gate.

CI runs the demo regression. The engineer running a slice should also run it locally on the branch tip and archive enough information to reproduce any diff.

## Performance gate (mandatory)

Performance is a first-class gate, not a note at the end of a refactor.

Canonical recorded performance measurement follows `testing.md` and `docs/benchmark-progress.json`. Baseline version, Docker-RSS caveat, timed-output validation requirement, and measurement command: see `testing.md § Baseline` and `testing.md § Timed-output validation`.

The provisional performance smoke benchmark is the chr22 testset described in `testing.md`. Smaller tests may be used during development, but performance claims must be grounded in a documented performance tier. The current chr22 tier is useful for smoke timing and has baseline genotype, switch, and flip errors; high-core scaling claims require a larger non-pathological tier recorded in `docs/benchmark-progress.json` unless the design note explicitly classifies the slice as exploratory or enabling-only.

Each slice records results in the machine-readable tracker at `docs/benchmark-progress.json`.

### Metrics

Record at minimum:

- wall-clock runtime;
- maximum resident set size;
- thread count;
- execution environment and binary path, image tag, or digest used for the measured `vcfdist` process;
- host CPU count and NUMA notes when available;
- baseline commit/tag and branch commit;
- baseline and measured output artifact directories;
- output validation command/script and log path;
- output-diff verdict for the timed output tree.

### Thread targets

The practical scaling target is efficient 64-core use. Use this thread sweep when the host supports it:

```text
1, 2, 8, 16, 32, 64
```

If a larger NUMA host is available, optional stretch measurements may add:

```text
128, 256
```

The key success metric is better scaling efficiency, followed by an eventual 2x wall-clock speedup on a documented performance benchmark large enough to exercise the target core count.

### Runtime acceptance

- A pure refactor must not show a meaningful slowdown on the documented performance benchmark used for that slice.
- A performance refactor may be accepted as enabling-only if the design note says what later optimization it enables and why the intermediate cost is acceptable.
- An optimization should show measured improvement in wall-clock runtime or scaling efficiency on the thread counts relevant to its design.

### Memory acceptance

Per-core memory usage must stay below `3.0x` relative to the baseline at the same thread count. Compare `(branch peak RSS / threads)` to `(baseline peak RSS / threads)`; at equal thread counts this is the same as requiring branch peak RSS below `3.0x` baseline peak RSS. The cap was relaxed from `1.3x` to `3.0x` on 2026-05-28 in exchange for higher wall-clock parallelism on the `HG00733_chr2_full` benchmark; see `testing.md` for the full rationale and absolute RSS figures. Increases beyond `3.0x` require explicit approval before implementation.

Do not trade speed for unbounded replication. Any design that duplicates reference data, variant data, superclusters, dynamic-programming matrices, output buffers, or thread-local caches must include memory accounting and peak RSS comparison.

## Goals

- Improve high-core scaling efficiency, with 64-core utilization as the near-term target.
- Preserve vcfdist's current output semantics, genotyping error rates, switch error rates, and flip error rates.
- Keep per-core memory growth under the documented threshold.
- Reduce serial bottlenecks, load imbalance, avoidable allocation churn, and scheduler overhead.
- Prefer data layouts and seams that support both C++ optimization and possible Rust kernel migration.
- Keep correctness gates cheap enough to run often and complete enough to catch behavior drift.

## Non-goals (explicit)

- **No correctness shortcuts.** A faster result is not acceptable if output, error rates, or comparison semantics change without an approved feature proposal.
- **No public CLI or output-format changes** as part of a performance slice unless separately approved.
- **No wholesale rename to a different style.** Existing naming conventions are documented in `coding-guidelines.md` and stay.
- **No design-pattern abstractions for their own sake.** The bar for an abstraction is that it enables a measured performance change, deletes more code than it adds, or makes a recurring bug class structurally impossible.
- **No new external dependency or production language runtime without approval.** Rust is allowed through the port/rewrite path above; other dependencies require the same level of review.

Algorithmic changes are allowed when the design is approved and the correctness gate remains green.

## First slice

Deferred. To be chosen after this harness cleanup lands.

The first slice should be selected from performance evidence, not file size alone. Candidate design notes should identify:

- the hot path or scaling bottleneck;
- the expected speedup mechanism;
- the memory-risk profile;
- the exact benchmark comparison to run;
- whether the slice is pure refactor, performance refactor, optimization, or port/rewrite experiment.

Examples of valid first-slice shapes include isolating a scheduler, flattening a hot data structure, reducing allocation churn, or extracting a kernel so it can be optimized or ported safely. Large-file decomposition is useful only when it supports one of those outcomes.

## Workflow per slice

For each slice:

1. **Branch from `master`.** Branch name: `refactor/<short-slice-name>` for pure/performance refactors, `perf/<short-slice-name>` for optimizations, or `port/<short-slice-name>` for approved port experiments.
2. **Classify the slice.** Record whether it is pure refactor, performance refactor, optimization, or port/rewrite experiment.
3. **Capture baselines.** Run the required correctness and benchmark commands on the `v2.6.4` baseline or the current documented baseline. Archive the timed output trees and timing/RSS summaries.
4. **Write a design note.** The note must include the performance hypothesis, correctness risk, memory-risk assessment, benchmark plan, and expected output diff (`none`).
5. **Get approval before implementation.** Design decisions are approved before code changes. Any algorithm, threading, memory-layout, language/runtime, dependency, CLI, or output-semantic change requires explicit approval.
6. **Implement in commits that each compile clean under `-Wall -Wextra`.** Small commits beat large ones. Behavior preservation per commit is the bar unless the approved design says an intermediate commit is non-final.
7. **Rerun correctness gates** on the branch tip and diff against archived baselines.
8. **Rerun performance gates** on the branch tip, validate the exact timed output tree against the archived baseline output tree, and compare wall-clock, scaling efficiency, and peak RSS against baseline.
9. **Update `docs/benchmark-progress.json`** with the measured result or with the reason the slice is enabling-only.
10. **Update `architecture.md`** when the file structure, data flow, or concurrency model changes.
11. **Merge only if gates and approvals are satisfied.** Once a design is approved, the implementation/verification loop runs to completion unless the gate fails or the design assumptions are proven wrong.

## What can go wrong

- **Benchmark noise.** Shared machines, I/O cache state, NUMA placement, and container overhead when Docker is used can obscure small wins. Record host details and prefer repeated medians for short runs.
- **NUMA regressions.** A change that scales on one socket may degrade on two or more sockets. Treat 128/256-core runs as stretch diagnostics until the 64-core path is healthy.
- **Memory replication.** Per-thread caches and duplicated matrices can win wall-clock while violating the per-core memory rule. Account for peak RSS.
- **Hidden ABI on `Globals`.** Many TUs include `globals.h` and reach into `Globals` members directly. Reordering or splitting fields in `Globals` will recompile most of `src/`. Minimize churn in `Globals` member layout during early slices.
- **Threading invariants.** Current parallelism is based on disjoint work partitions. Any code motion across thread boundaries must preserve output disjointness or explicitly justify synchronization.
- **HTSlib resource leaks.** Resource handles (`htsFile*`, `bcf_hdr_t*`, `bcf1_t*`, `faidx_t*`) are freed at explicit points. A refactor that moves code across an early-return must re-check every `bcf_destroy` / `hts_close` path.
- **Inlining and optimizer changes.** The Makefile defaults to `-O3`. Moving hot helpers can change inlining and throughput even when source behavior is unchanged.

## Exit criteria for this plan

This plan is healthy when:

1. The first approved slice has completed with green correctness gates.
2. `docs/benchmark-progress.json` contains baseline and branch measurements for that slice.
3. The result shows either a measured performance improvement or a documented enabling step toward one.
4. `architecture.md` reflects any changed file structure, data flow, or concurrency model.
5. The next slice decision is based on the tracker and benchmark evidence.

Long-term success is measured by better scaling efficiency, eventual 2x wall-clock speedup on a documented performance benchmark large enough to exercise the target core count, unchanged output/error rates, and bounded per-core memory.
