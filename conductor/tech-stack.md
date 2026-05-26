# Tech Stack

## Implementation language and build

- Primary implementation: C++17.
- Build system: hand-written GNU Make under `src/`.
- Required compile flags: `-std=c++17 -Wall -Wextra -O3`.
- Required build hygiene: `make -C src` must succeed with no new warnings.
- Source layout: single `src/` tree; implementation/header pairs live together; no `include/` split.
- Existing code has no namespaces and uses the conventions in `docs/coding-guidelines.md`.

## Link-time and runtime dependencies

- HTSlib for VCF/BCF, FASTA/FAI, and BGZF handling.
- pthread for the existing multithreaded precision/recall path.
- zlib.
- libm.

New external dependencies, build-system changes, or production language runtimes require explicit design approval. Rust or another language may be introduced only through an approved `port/rewrite` slice; it is not incidental cleanup.

## Current architecture snapshot

All major production sources are in `src/`:

- `main.cpp` — CLI-driven pipeline driver.
- `globals.h` / `globals.cpp` — global configuration singleton and CLI parsing.
- `variant.h` / `variant.cpp` — VCF-backed variant containers.
- `bed.h` / `bed.cpp` — optional BED region handling.
- `fasta.h` — header-only FASTA wrapper.
- `cluster.h` / `cluster.cpp` — clustering and superclustering.
- `dist.h` / `dist.cpp` — precision/recall, edit distance, BiWFA distance routines, and current thread driver.
- `phase.h` / `phase.cpp` — phase-block and switch/flip analysis.
- `edit.h` / `edit.cpp` — edit-distance summary structures.
- `print.h` / `print.cpp` — human-readable INFO output and report writers.
- `timer.h` / `timer.cpp` — stage timers.

Known large or structurally important files: `dist.cpp`, `cluster.cpp`, `variant.cpp`, `print.cpp`, and `globals.cpp`. `dist.cpp` is the main early performance-investigation candidate when benchmark evidence supports it.

## Execution and benchmarking environment

- Canonical benchmark environment: Docker.
- Baseline image: `timd1/vcfdist:v2.6.4`.
- Native runs are useful for diagnosis but are not recorded as canonical benchmark results unless a design note explicitly says otherwise.
- Timing tool: `/usr/bin/time -v`.
- Required recorded metrics: wall-clock runtime, maximum resident set size, thread count, image/tag or digest, host CPU/NUMA notes, baseline and branch commit/tag, and output-diff verdict.
- Preferred thread sweep on capable hosts: `1, 8, 16, 32, 64`.
- Optional NUMA stretch sweep: `128, 256`.

## Coding and performance constraints

- Match existing naming/layout conventions; do not rename symbols for style.
- Do not change public CLI or output formats as part of a performance slice without separate approval.
- Current parallelism partitions superclusters in `dist.cpp::precision_recall_threads_wrapper`; new threading designs require approval and must describe scheduling, synchronization, scaling benefit, and peak-memory effect.
- Avoid pointer-heavy hot-path data structures, avoid unbounded per-thread replication, and account for memory when adding caches, buffers, duplicated inputs, or dynamic-programming matrices.
