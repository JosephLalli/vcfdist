# Plan: Refactor vcfdist Monolithic Codebase

This plan tracks the Conductor-level workflow for the first and subsequent performance-refactor slices. Detailed slice notes live in the orchestrator record and, when merged, optionally under `docs/dev/`.

## Tasks

- [ ] Read and confirm the current source-of-truth docs: `AGENTS.md`, `docs/refactoring-plan.md`, `docs/benchmark-progress.json`, `testing.md`, `docs/architecture.md`, `docs/coding-guidelines.md`, and `docs/multiagent-process.md`.
- [ ] Establish or confirm the demo regression correctness baseline and the documented native `/usr/bin/time -v` performance baseline, including archived timed output trees for benchmark diffs.
- [ ] Run Explorer Discovery to identify candidate slices from source structure and benchmark evidence.
- [ ] Record each Explorer candidate with touched files/symbols, suspected bottleneck, expected speedup mechanism, memory-risk profile, and blast radius.
- [ ] Have Oracle choose one candidate and classify it as pure refactor, performance refactor, optimization, or port/rewrite experiment.
- [ ] Write the Oracle design note with target seams, performance hypothesis, correctness risk, memory-risk assessment, benchmark plan, and expected output diff (`none`).
- [ ] Obtain explicit approval before implementation, especially for algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes.
- [ ] Create the implementation branch from `master` using the approved prefix: `refactor/`, `perf/`, or `port/`.
- [ ] Have Fixer implement the approved slice in small commits that compile clean under `-Wall -Wextra`.
- [ ] Run intermediate checks such as `make -C src -j24`, `bash demo/regression.sh`, `demo/demo.sh`, or smaller local fixtures as needed during implementation.
- [ ] Run the demo regression correctness gate on the branch tip and diff outputs against the baseline.
- [ ] Run the documented performance gate with the planned thread counts, validate the exact timed output trees against archived baselines, and record wall-clock/runtime scaling plus peak RSS.
- [ ] Have Librarian produce a pass/fail verification verdict covering output diffs, genotyping/switch/flip error rates, wall-clock comparison, peak RSS comparison, and tracker consistency.
- [ ] Update `docs/benchmark-progress.json` with baseline and branch measurements, or with the enabling-only rationale if no speedup claim is made.
- [ ] Update `docs/architecture.md` if the slice changes file structure, data flow, concurrency model, or language/runtime boundaries.
- [ ] Merge only after gates and approvals are satisfied.
- [ ] Use tracker results and benchmark evidence to choose the next slice; return to Discovery.

## Open measurement gaps to close early

- [ ] Record or refresh native `/usr/bin/time -v` performance baseline results for thread counts `1, 2, 8, 16, 32, 64`.
- [ ] Record host CPU count, NUMA topology, local binary build identifier, and storage notes for benchmark hosts.
- [ ] Add a checked-in helper or documented command for benchmark output-tree validation and tracker-level genotyping/switch/flip metric extraction.
- [ ] Add or reference a larger non-pathological benchmark tier before making high-core scaling claims.
