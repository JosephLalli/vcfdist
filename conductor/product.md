# Product Context

## Product

`vcfdist` is a variant-comparison tool. It compares one query VCF against one truth VCF using a reference FASTA and, optionally, a BED file of evaluation regions. It evaluates precision/recall, genotyping error, phasing/switch error, edit distance, and writes per-variant annotations plus summary TSV/VCF reports. Output is an on-disk report tree controlled by `-p` / `--prefix`.

## Current refactor mission

This Conductor project exists to manage a brownfield performance refactor of the existing monolithic C++ codebase. The objective is to make repeated performance work safe, measurable, and reviewable while preserving the correctness and public behavior of the released tool.

Primary goals:

- Reduce wall-clock runtime in high-core environments.
- Improve scaling efficiency, with 64-core utilization as the near-term target.
- Treat 128/256-core NUMA scaling as a stretch target.
- Preserve output files, comparison semantics, genotyping error rates, and switch error rates unless a separate approved feature proposal changes them.
- Keep per-core memory growth bounded; branch peak RSS at a matching thread count should remain below `1.3x` baseline peak RSS unless explicitly approved.
- Use performance evidence, not file size alone, to select refactor slices.

Long-term success is better high-core scaling, an eventual 2x wall-clock speedup on the canonical chr21 benchmark, unchanged correctness metrics, and a benchmark tracker that can guide the next slice from evidence.

## Canonical correctness and benchmark fixture

The canonical fixture is `fixtures/hg03784_chr21_grch38/`, derived from HPRC sample `HG03784` on `chr21` / `GRCh38`. It exercises the phased-comparison path.

Inputs:

- `fixtures/hg03784_chr21_grch38/query.vcf.gz`
- `fixtures/hg03784_chr21_grch38/truth.bcf`
- `fixtures/hg03784_chr21_grch38/reference.fa`
- `fixtures/hg03784_chr21_grch38/region.bed`

Authoritative baseline for correctness diffs and performance comparisons: `v2.6.4` / Docker image `timd1/vcfdist:v2.6.4`.

## Key source-of-truth documents

- `AGENTS.md` — phase taxonomy, agent roles, and information flow.
- `docs/refactoring-plan.md` — goals, non-goals, gates, workflow, and acceptance rules.
- `docs/benchmark-progress.json` — live benchmark tracker and measurement gaps.
- `testing.md` — canonical fixture, Docker command, and benchmark protocol.
- `docs/architecture.md` — current source map, data flow, hotspots, and structural tensions.
- `docs/coding-guidelines.md` — conventions that refactor work must preserve.
- `docs/multiagent-process.md` — Conductor/orchestrator-facing process notes.
