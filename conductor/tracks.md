# Tracks Index

Tracks are Conductor-owned work streams. The active project track is the brownfield performance refactor of the existing vcfdist monolith.

- [ ] `refactor-monolith` — Refactor vcfdist Monolithic Codebase
  - Status: pending first approved slice.
  - Mission: improve high-core scaling and eventual wall-clock runtime while preserving output correctness and bounded per-core memory.
  - Canonical docs: `docs/refactoring-plan.md`, `docs/architecture.md`, `docs/benchmark-progress.json`, `testing.md`, `docs/coding-guidelines.md`, `docs/multiagent-process.md`, `AGENTS.md`.
  - Canonical fixture: `fixtures/hg03784_chr21_grch38/`.
  - Baseline: `v2.6.4` / `timd1/vcfdist:v2.6.4`.
