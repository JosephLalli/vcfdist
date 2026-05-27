# Product Context

## Product

`vcfdist` is a variant-comparison tool. It compares one query VCF against one truth VCF using a reference FASTA and, optionally, a BED file of evaluation regions. It evaluates precision/recall, genotyping error, phasing/switch error, edit distance, and writes per-variant annotations plus summary TSV/VCF reports. Output is an on-disk report tree controlled by `-p` / `--prefix`.

## Current refactor mission

This Conductor project exists to manage a brownfield performance refactor of the existing monolithic C++ codebase. The objective is to make repeated performance work safe, measurable, and reviewable while preserving the correctness and public behavior of the released tool.

Primary goals:

- Reduce wall-clock runtime in high-core environments.
- Improve scaling efficiency, with 64-core utilization as the near-term target.
- Treat 128/256-core NUMA scaling as a stretch target.
- Preserve output files, comparison semantics, genotyping error rates, switch error rates, and flip error rates unless a separate approved feature proposal changes them.
- Keep per-core memory growth bounded; branch peak RSS at a matching thread count should remain below `1.3x` baseline peak RSS unless explicitly approved.
- Use performance evidence, not file size alone, to select refactor slices.

Long-term success is better high-core scaling, an eventual 2x wall-clock speedup on a documented performance benchmark large enough to exercise the target core count, unchanged correctness metrics, and a benchmark tracker that can guide the next slice from evidence.

## Correctness gate and performance fixture

The canonical correctness gate is `bash demo/regression.sh`, which runs the bundled chr1 5 Mb demo and compares output against `demo/results/`. This is the first regression gate because it includes superclusters plus nonzero genotype, switch, and flip errors.

The bundled performance smoke fixture is `fixtures/HG00733_chr22_32000000_37000000_phaseflip/`, derived from HPRC sample `HG00733` on `chr22` / `GRCh38`. It exercises the phased-comparison path with baseline genotype, switch, and flip errors but is not the correctness gate.

Inputs:

- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/query.1kgp.phaseflip.bcf`
- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/truth.hprc.bcf`
- Reference FASTA: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta` (must be available at this path)
- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/region.bed`

Baseline version, Docker-RSS caveat, and timed-output validation requirement: see `testing.md § Baseline` and `testing.md § Timed-output validation`.

## Key source-of-truth documents

- `AGENTS.md` — agent-facing entry point. Phase taxonomy and delegation map: `docs/agents/index.md`. Detailed roles: `docs/agents/`.
- `docs/refactoring-plan.md` — goals, non-goals, gates, workflow, and acceptance rules.
- `docs/benchmark-progress.json` — live benchmark tracker and measurement gaps.
- `testing.md` — demo correctness gate, chr22 performance command, and benchmark protocol.
- `docs/architecture.md` — current source map, data flow, hotspots, and structural tensions.
- `docs/coding-guidelines.md` — conventions that refactor work must preserve.
- `docs/multiagent-process.md` — Conductor/orchestrator-facing process notes.
