# Workflow

## Operating model

Conductor coordinates performance-refactor slices through the phase taxonomy defined in `AGENTS.md`:

1. **Discovery** — understand the current code, benchmark evidence, and candidate bottlenecks.
2. **Design** — choose one slice shape, define seams, document the speedup hypothesis, and assess memory/correctness risk.
3. **Implementation** — make the approved change in small compiling commits.
4. **Verification** — run correctness and performance gates, record results, and clear or fail the slice.

Information flow:

```text
Explorer → Oracle → Fixer → Librarian → (merge or back to Oracle)
```

If the verification gate fails, control returns to Oracle for redesign rather than treating Fixer/Librarian as a retry loop.

## Agent responsibilities

### Explorer — Discovery

Reads `src/`, `docs/architecture.md`, `docs/benchmark-progress.json`, benchmark notes, and prior discovery output. Produces candidate-slice notes that name files, symbols, line ranges, benchmark observations, suspected hot paths, expected speedup mechanisms, memory-risk profiles, and blast radius.

Selection rule: performance evidence beats size. A large function/file is a candidate only when decomposition supports measurement, isolation, correctness, or optimization of a hot path.

### Oracle — Design

Consumes Explorer output and `docs/refactoring-plan.md`. Picks at most one candidate per cycle and writes a design note that classifies the slice as pure refactor, performance refactor, optimization, or port/rewrite experiment.

The design note must state target seams, performance hypothesis, correctness risk, memory-risk assessment, benchmark plan, and expected output diff (`none`). Design approval is mandatory before implementation.

### Fixer — Implementation

Implements the approved design on a branch:

- `refactor/<short-slice-name>` for pure/performance refactors.
- `perf/<short-slice-name>` for optimizations.
- `port/<short-slice-name>` for approved port/rewrite experiments.

Each commit should compile clean under `-Wall -Wextra`; small commits beat large ones. Behavior preservation per commit is expected unless the approved design explicitly allows a non-final intermediate.

### Librarian — Verification

Enforces the gates from `docs/refactoring-plan.md` and `testing.md`. Produces a pass/fail verdict covering the output-diff status of the exact timed benchmark output tree, genotyping/switch/flip error status, wall-clock comparison, peak RSS comparison, and tracker consistency.

The gates are not negotiable per slice. If a gate is wrong, update the governing docs separately.

## Per-slice workflow

1. Branch from `master` using the appropriate prefix.
2. Classify the slice before implementation.
3. Capture baseline correctness and benchmark runs against the documented `v2.6.4` baseline, currently the local `./src/vcfdist` binary timed directly by `/usr/bin/time -v` as recorded in `docs/benchmark-progress.json`; archive the timed benchmark output tree for later diffs.
4. Write a design note with performance hypothesis, correctness risk, memory risk, benchmark plan, and expected output diff (`none`).
5. Get approval before implementation, especially for algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes.
6. Implement in small commits that compile cleanly.
7. Rerun correctness gates on the branch tip and diff against archived baseline outputs.
8. Rerun performance gates, validate the exact timed benchmark output tree against the archived baseline output tree, and compare wall-clock runtime, scaling efficiency, and peak RSS.
9. Update `docs/benchmark-progress.json` with measurements or the enabling-only rationale.
10. Update `docs/architecture.md` if file structure, data flow, concurrency, or language boundaries changed.
11. Merge only after gates and approvals are satisfied.

## Mandatory gates

Correctness gate:

- Build `src/vcfdist` and run `bash demo/regression.sh`.
- Branch output tree must match the checked-in `demo/results/` baseline under the comparison rules in `testing.md`.
- Genotyping, switch, and flip error rates must not change.
- Smaller ad hoc fixtures are useful during development but do not replace the demo regression gate.

Performance gate:

- Use the recorded benchmark environment and command from `testing.md` / `docs/benchmark-progress.json`; currently this is native direct timing of `./src/vcfdist` with `/usr/bin/time -v`.
- Use the documented performance benchmark tier for the slice; the bundled chr22 testset is only a provisional smoke tier until a larger tier is selected.
- Record `/usr/bin/time -v` wall-clock runtime and peak RSS for the measured `vcfdist` process. Do not use host `/usr/bin/time -v docker run ...` RSS as `vcfdist` RSS.
- Validate the output tree produced by that same timed `vcfdist` invocation; validation runs after `/usr/bin/time` exits and is not included in wall-clock runtime.
- Prefer thread sweep `1, 2, 8, 16, 32, 64` when host capacity allows.
- Compare branch and baseline at matching thread counts.
- Keep per-core memory growth below 30% unless explicitly approved.

## Non-goals for routine slices

- No correctness shortcuts.
- No public CLI or output-format changes without separate approval.
- No wholesale renaming to a different style.
- No abstractions for their own sake.
- No new external dependency or production runtime without approval.
