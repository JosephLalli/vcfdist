# Tracks Index

Conductor-owned work streams.

- [ ] `refactor-monolith` — Refactor vcfdist Monolithic Codebase
  - Status: active; several optimization slices merged, no slice currently in progress.
  - Mission: improve high-core scaling and eventual wall-clock runtime while preserving output correctness and bounded per-core memory.
  - Canonical docs: see `conductor/product.md` for the source-of-truth pointer table.
  - Canonical fixtures and baseline: `testing.md` (`HG00733_chr2_full` for performance, `HG00733_chr22_32000000_37000000_phaseflip` as smoke, `v2.6.4` baseline).
