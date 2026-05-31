# Product Context

## Product

`vcfdist` is a variant-comparison tool. It compares one query VCF against one truth VCF using a reference FASTA and an optional BED of evaluation regions; it evaluates precision/recall, genotyping/switch/flip errors, and edit distance, and writes per-variant annotations plus summary TSV/VCF reports.

## Current refactor mission

Brownfield performance refactor of the existing monolithic C++ codebase. Goal: make repeated performance work safe, measurable, and reviewable while preserving the correctness and public behavior of the released tool. Near-term scaling target is efficient 64-core utilization; long-term goal is a 32x wall-clock speedup on a documented benchmark large enough to exercise the target core count.

## Source-of-truth docs

| For | See |
|---|---|
| Goals, gates, per-slice workflow, tried-and-retired | `docs/refactoring-plan.md` |
| Architecture and structural tensions | `docs/architecture.md` |
| Live benchmark tracker | `docs/benchmark-progress.json` |
| Testing protocol, fixtures, benchmark command | `testing.md` |
| Coding conventions, correctness/performance invariants (incl. the `3.0x` per-core RSS cap) | `docs/coding-guidelines.md` |
| Agent delegation and process | `docs/agents/index.md` |
| Top-level table of contents | `AGENTS.md` |
| Broad doc map | `INFRASTRUCTURE_SUMMARY.md` |
