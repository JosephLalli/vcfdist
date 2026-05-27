# Tracks Index

Tracks are Conductor-owned work streams. The active project track is the brownfield performance refactor of the existing vcfdist monolith.

- [ ] `refactor-monolith` — Refactor vcfdist Monolithic Codebase
  - Status: pending first approved slice.
  - Mission: improve high-core scaling and eventual wall-clock runtime while preserving output correctness and bounded per-core memory.
  - Canonical docs: `docs/refactoring-plan.md`, `docs/architecture.md`, `docs/benchmark-progress.json`, `testing.md`, `docs/coding-guidelines.md`, `docs/multiagent-process.md`, `AGENTS.md`.
  - Canonical fixture: `fixtures/HG00733_chr22_32000000_37000000_phaseflip/`.
  - Baseline: `v2.6.4` — see `testing.md § Baseline` for the measurement protocol and Docker-RSS caveat.
