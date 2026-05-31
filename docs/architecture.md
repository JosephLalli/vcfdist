# vcfdist Architecture

A snapshot of the codebase as it stands. This document records what is, not what should be — refactoring goals live in `refactoring-plan.md`.

## What vcfdist does

vcfdist compares a query VCF against a truth VCF, evaluates precision/recall and phasing accuracy, and writes per-variant annotations plus summary reports. Input is one query VCF, one truth VCF, a reference FASTA, and optionally a BED of evaluation regions. Output is a directory of TSV/VCF reports controlled by `-p`/`--prefix`.

Correctness fixture, benchmark protocol, direct `/usr/bin/time -v` invocation, and same-run benchmark output validation requirement: see `testing.md` and `demo/demo.sh`.

## Module map

All sources live in `src/`. Headers and implementation files come in matching pairs; `fasta.h` is header-only.

### Entry point and configuration

- `main.cpp` (292 lines) — `int main(argc, argv)`. Parses args via `g.parse_args`, sets up the timer set, reads the reference FASTA and the two VCFs into `variantData` instances, then drives the comparison pipeline: cluster → supercluster → precision/recall → optional edit distance → phase → write.
- `globals.h` / `globals.cpp` (655 lines) — declares `class Globals` and the program-wide instance `extern Globals g;` (defined in `main.cpp`). `Globals::parse_args` is ~491 of the 655 lines — `globals.cpp` is overwhelmingly CLI handling, not stateful logic. The actual configuration surface (`Globals` member fields) is one screen of defaults in `globals.h`.
- `defs.h` — shared constants, type tags, and the `INFO`/`WARN`/`ERROR` logging macros used throughout the codebase.

### Data containers

- `variant.h` / `variant.cpp` (1004 lines) — `ctgVariants` (per-contig, parallel arrays of variant fields) and `variantData` (top-level VCF holder: `variants[hap][ctg] -> shared_ptr<ctgVariants>`). The `variantData` constructor reads a VCF via HTSlib; `write_vcf` and `left_shift` are the other large methods.
- `bed.h` / `bed.cpp` (288 lines) — `bedData` and `contigRegions`. Parses the optional `-b` BED and answers in/out-of-region queries.
- `fasta.h` (header-only) — `fastaData`. Holds reference sequence strings and contig lengths. When a BED is provided, `main.cpp` passes the BED contig list so `fastaData` can load only those contigs through an existing HTSlib `.fai` index; if the index is absent it falls back to kseq streaming filtered to the requested contigs. Without a BED, `main.cpp` parses query/truth headers and records first, builds the ordered union of observed query then truth contigs, and loads only those reference contigs before validation/writing; empty no-record runs fall back to full-reference loading. The object is held as `std::shared_ptr<fastaData>` and threaded through everything that needs reference sequence.
- `cluster.h` / `cluster.cpp` (1285 lines) — `ctgSuperclusters` and `superclusterData`. Groups variants into clusters (per haplotype) and then into superclusters (cross-haplotype regions that must be evaluated together). The main clustering entry points are `simple_cluster(...)`, `wf_swg_cluster(...)`, and `superclusterData::supercluster(...)`; the rest is the BiWFA-driven realignment path selected by `--cluster biwfa`.
- `phase.h` / `phase.cpp` (626 lines) — `ctgPhaseblocks`, `phaseblockData`, and the phase-block analysis that runs after precision/recall.
- `edit.h` / `edit.cpp` (280 lines) — `editData` and the edit-distance summary across the comparison.

### Algorithms

- `dist.cpp` (2867 lines) — the bulk of the comparison logic, including:
  - `precision_recall_threads_wrapper` and `precision_recall_wrapper`: the multithreaded driver over superclusters.
  - `edits_wrapper`: edit-distance summarization.
  - `count_dist`, the BiWFA distance routines, and the wave/queue helpers (`contains(...)` etc.).
  - `calc_ng50` and other phasing/NGA50 utilities.

  This file is the largest single source in the repo (~31% of `src/` LOC) and is a likely candidate for early performance investigation if benchmark evidence points there.

### Output

- `print.cpp` (905 lines) — formats all human-readable INFO output and all per-variant report files. Most of the output paths (precision/recall tables, supercluster reports, phase reports, summary VCFs) live here.

### Support

- `timer.h` / `timer.cpp` — thin `class timer` wrapping a chrono clock. The `Globals::timers` vector holds one entry per pipeline stage; stages start/stop their own timer.

## Data flow

`main()` runs the pipeline sequentially:

1. **Read** — `fastaData` (reference; BED-scoped when `-b/--bed` is present; otherwise loaded after query/truth parsing from the ordered union of observed input contigs), `variantData` (query), `variantData` (truth).
2. **Cluster** — variants are clustered per haplotype, then superclustered across haplotypes (`cluster.cpp`). BiWFA clustering launches outer haplotype/contig tasks and, when few outer tasks exist, assigns bounded inner workers to active-cluster reach computation while preserving serial merge/order behavior.
3. **Realign** (optional, gated by `--realign-query` / `--realign-truth`) — left-shifts and re-emits the input VCFs. `wf_swg_realign` in `dist.cpp` dispatches work items via a work-stealing thread pool bounded by a per-claim RAM budget (`min(max_ram * 30%, 3 GB)`); oversized single clusters are serialized against the full budget rather than blocking indefinitely.
4. **Precision / recall** — `precision_recall_threads_wrapper` in `dist.cpp` partitions superclusters across threads and computes TP/FP/FN with credit via BiWFA. Large memory groups run the four independent query/truth haplotype alignments in parallel; very large superclusters in the lowest memory group can also reserve a small global budget of nested alignment workers based on the existing RAM-step estimate.
5. **Edit distance** (optional, gated by `--distance`) — `edits_wrapper` in `dist.cpp` summarizes edit distance across the comparison and writes distance reports.
6. **Phase analysis** — `phaseblockData` in `phase.cpp` annotates phase blocks and switch/flip errors.
7. **Write** — `print.cpp` emits the report TSVs and (if applicable) the rewritten VCFs.

The pipeline is single-process. Parallelism exists in BiWFA clustering/reclustering, the optional realign stage, and precision/recall. `main.cpp` bounds clustering inner workers by available outer haplotype/contig tasks; `wf_swg_realign` uses a work-stealing pool bounded by a RAM budget; and `dist.cpp::precision_recall_threads_wrapper` bounds nested precision/recall alignment workers with a global atomic budget.

An intra-alignment score-wave parallelism experiment (one OpenMP team per giant `--exact-prec-recall` alignment, barrier between score waves) was tried on branch `perf/pr-wavefront-slice0-measure` and was a NO-GO due to anti-scaling: the per-wave serial merge dominates. The code is disabled by default (`g_pr_intra_team = 1` in `dist.cpp`) and the branch is parked. See `docs/refactoring-plan.md § Tried and retired` and `docs/benchmark-progress.json` (entry `pr_wavefront_intra_team`) for the full result. The outer threading model described above is the only active PR concurrency path.

## Dependency graph (from Makefile)

```
globals.o    : globals.cpp globals.h bed.h print.h defs.h timer.h
print.o      : print.cpp   print.h   globals.h phase.h dist.h edit.h defs.h variant.h
timer.o      : timer.cpp   timer.h   globals.h defs.h
variant.o    : variant.cpp variant.h print.h fasta.h defs.h globals.h
dist.o       : dist.cpp    dist.h    fasta.h variant.h cluster.h print.h edit.h defs.h globals.h
bed.o        : bed.cpp     bed.h     print.h defs.h globals.h
edit.o       : edit.cpp    edit.h    defs.h globals.h
cluster.o    : cluster.cpp cluster.h variant.h fasta.h globals.h dist.h defs.h
phase.o      : phase.cpp   phase.h   cluster.h print.h globals.h defs.h variant.h
```

`globals.h` and `defs.h` are pulled by nearly everything. `print.h` is pulled by most of the data-container TUs (which call `INFO(...)` and friends). `dist.h` and `cluster.h` are mutually known to each other.

## File sizes (lines)

| File | Lines | Role |
|---|---:|---|
| `dist.cpp` | 2867 | precision/recall, edit distance, BiWFA, threading |
| `cluster.cpp` | 1285 | clustering and superclustering |
| `variant.cpp` | 1004 | VCF read/write, `ctgVariants`/`variantData` |
| `print.cpp` | 905 | all reporting and INFO output |
| `globals.cpp` | 655 | mostly CLI parsing (`parse_args` ≈ 491 lines) |
| `phase.cpp` | 626 | phase-block analysis |
| `bed.cpp` | 288 | BED region handling |
| `edit.cpp` | 280 | edit-distance summary |
| `main.cpp` | 292 | pipeline driver |
| `timer.cpp` | 31 | timer primitives |

Total: ~9.2k lines across `src/`.

## Known structural tensions

These are recorded as observations, not as a refactoring plan. Acting on any of them is a separate decision tracked in `refactoring-plan.md`.

- `dist.cpp` mixes the threading driver, the BiWFA distance kernel, the precision/recall accounting, and edit-distance summarization in one translation unit. Any of those four concerns could plausibly become its own file.
- `globals.cpp` mixes configuration *data* (the `Globals` fields, declared in `globals.h`) with a very long `parse_args` implementation. Extracting the parser leaves `globals.cpp` short.
- `print.cpp` carries both formatted INFO logging and the on-disk report writers; those have different audiences and lifecycles.
- `Globals` is a singleton accessed as `g.` everywhere. Threading through it explicitly is feasible but would touch every TU.
- HTSlib resource handling is open-coded at each call site rather than wrapped in RAII. Consistent, but verbose.
- The current architecture records only where parallelism exists, not how well it scales. Performance slices should update this document when they change scheduling, memory layout, thread ownership, or Rust/C++ boundaries.
