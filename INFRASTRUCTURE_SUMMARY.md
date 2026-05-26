# Performance Refactor Harness Index

One-page map of the documents that drive vcfdist performance refactor work. Read top-to-bottom on first encounter.

- [`AGENTS.md`](AGENTS.md) — the four agent roles (Explorer, Oracle, Fixer, Librarian), the phase taxonomy, and the information flow. The orchestrator (Conductor) is the runtime; this file is the brief.
- [`docs/architecture.md`](docs/architecture.md) — snapshot of `src/` as it stands: modules, data flow, file sizes, known structural tensions. The Discovery phase starts here.
- [`docs/refactoring-plan.md`](docs/refactoring-plan.md) — performance-first goals, non-goals, the **correctness and performance gates** that every slice must pass, and the per-slice workflow.
- [`docs/coding-guidelines.md`](docs/coding-guidelines.md) — the conventions observed in `src/`. New code matches existing code.
- [`docs/multiagent-process.md`](docs/multiagent-process.md) — orchestrator-facing process notes (artifact locations, loop boundaries). Companion to `AGENTS.md`.
- [`testing.md`](testing.md) — the `HG03784` chr21 GRCh38 correctness/performance fixture, Docker benchmark command, and measurement protocol.
- [`docs/benchmark-progress.json`](docs/benchmark-progress.json) — machine-readable live tracker for baseline measurements, slice results, scaling targets, and measurement gaps.
- [`demo/demo.sh`](demo/demo.sh) — the chr1 5 Mb demo, useful as a fast intermediate smoke check.
- [`conductor/`](conductor/) — owned by the Conductor orchestrator; not edited from the repo side.

If you're acting as a refactor engineer (or as the Fixer agent), the sequence is: read `AGENTS.md` → `docs/refactoring-plan.md` → `docs/benchmark-progress.json` → `testing.md` → `docs/architecture.md` → `docs/coding-guidelines.md` → start an approved slice branch.
