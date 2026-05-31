# vcfdist Coding Guidelines

These guidelines describe how the existing vcfdist source is written. New code should match. The goal is to keep the codebase coherent during refactoring, not to import an outside style.

## Core invariants

### Correctness

- Output files, genotyping error rates, switch error rates, and flip error rates must not change unless a separate approved feature proposal explicitly changes public semantics.
- No branch merges until the demo regression gate is green. See `testing.md § Correctness regression gate`.

### Performance

- **Profile before optimizing.** No optimization or performance refactor starts without a baseline profile (`perf` / flamegraph / callgrind) that pins the hot path at function+line granularity and a whole-program payoff bound ("best case if this part goes to zero"). Instrumenting the inputs to a cost model (cell counts, wave widths) is not profiling. If the payoff is small, stop before designing.
- **Spike before plan.** Prototype the riskiest assumption (usually "will this actually be faster?") and run an isolation sweep on a small fixture before writing the production plan. Any parallelism ceiling must model the design's serial join cost (per-iteration merge/reduce/barrier), not just the problem's latent parallelism, and must be calibrated against at least one real measurement. See `refactoring-plan.md § Workflow per slice`.
- Optimize for high-core wall-clock runtime first (64-core scaling is the near-term target).
- Per-core memory growth must stay below `3.0x` relative to baseline at the same thread count (rationale in `testing.md`).
- Do not trade correctness for speed.

### Authorship

- Do not add AI-attribution markers to code, docs, or commit messages. The drift checker `tools/check_agent_docs.sh` enforces this.

## Language and build

- C++17. The Makefile sets `-std=c++17 -Wall -Wextra -O3`. New code must compile clean under those flags.
- Single source tree at `src/`; one header per `.cpp`. No `include/` split.
- Link-time dependencies: `htslib` (VCF/BCF, FASTA, BGZF), pthread, zlib, libm. New external dependencies should be discussed before being added.
- Rust or another implementation language may be introduced only by an approved port/rewrite slice under `docs/refactoring-plan.md`. Do not add language runtimes, build-system complexity, or dependencies as incidental cleanup.

## Conventions observed in `src/`

Documented here so they can be matched, not invented.

### Naming

- **Data classes**: lowerCamel — `ctgVariants`, `variantData`, `bedData`, `superclusterData`, `ctgPhaseblocks`, `phaseblockData`, `fastaData`, `editData`. Conventionally a `ctg*` prefix is used for per-contig containers and a `*Data` suffix for top-level multi-contig holders.
- **Process/state singletons**: PascalCase — `Globals` (declared in `globals.h`, defined as `extern Globals g;`).
- **Small structs**: lowerCamel — `var_info`, `contigRegions`, `idx1`, `idx2`.
- **Functions and methods**: `snake_case` — `left_shift`, `write_vcf`, `add_variants`, `parse_args`, `precision_recall_wrapper`, `count_dist`.
- **Members**: unprefixed `snake_case` — `ref_fasta_fn`, `cluster_method`, `phase_threshold`, `n`. No `m_` prefix.
- **Constants** that need program-wide visibility live as `const`/`constexpr` members of `Globals` (e.g. `VERSION`, `PROGRAM`) or as enum-like `int` constants in `defs.h`.

### Header layout

- Guards are `_FILENAME_H_` (uppercase with underscore wrappers), matching every existing header. Example:

```cpp
#ifndef _VARIANT_H_
#define _VARIANT_H_
// ...
#endif
```

- No namespaces. Symbols live at file scope; collisions are managed by class membership and by the small surface area of each header.
- `#include` order:
  1. C++ standard library: `<algorithm>`, `<string>`, `<vector>`, etc.
  2. HTSlib: `#include "htslib/vcf.h"`, `#include "htslib/faidx.h"`, etc.
  3. Local headers: `#include "defs.h"`, `#include "variant.h"`, etc.

  Blank line between groups.

### Source file layout

- Implementation file matches its header (`foo.cpp` ↔ `foo.h`).
- Forward declarations live at the top of the header where they are needed.
- Free functions used across files (e.g. `parent_path`, `create_directory` from `globals.h`) are declared in the header that defines their semantic home.

## Memory and ownership

- Resource ownership that crosses container boundaries uses `std::shared_ptr`. The codebase already uses `std::shared_ptr<fastaData>` (reference shared across `variantData` and the comparison pipeline) and `std::shared_ptr<ctgVariants>` (per-contig variant containers held inside `variantData`).
- Stack allocation is the default. Prefer values and references; reach for `shared_ptr` only when the object outlives the scope that created it.
- No raw `new` / `delete` in new code. HTSlib C handles (e.g. `htsFile*`, `bcf_hdr_t*`, `bcf1_t*`) are owned by short-lived local scopes and freed explicitly — match the existing pattern (`bcf_destroy`, `bcf_hdr_destroy`, `hts_close`) when adding new HTSlib code.

## Performance and memory invariants

- Performance work optimizes high-core wall-clock runtime first, especially 64-core scaling. NUMA-aware 128/256-core behavior is a stretch target.
- Keep hot-path data layouts cache-friendly. Avoid adding pointer-heavy containers, node-based hash tables, or per-element heap allocation to inner loops without a measured reason.
- Per-core memory usage must stay within the limit in `docs/refactoring-plan.md`. Any new thread-local cache, duplicated input structure, dynamic-programming buffer, or output buffer must have an explicit memory-risk note in the design.
- Prefer bounded reusable buffers over repeated allocation in hot paths, but do not introduce global mutable caches without a threading and memory accounting plan.
- Do not trade correctness for speed. Output files, genotyping error rates, switch error rates, and flip error rates must remain unchanged unless a separate feature proposal changes the public semantics.

## Const correctness and inlining

- Methods that don't mutate `*this` are marked `const`.
- Container parameters passed by `const` reference (`const std::vector<int> &`, `const std::string &`).
- Small hot helpers in `dist.cpp` use `inline` (see `contains(...)`); keep that pattern for new hot-path helpers, otherwise leave inlining to the compiler.

## Threading model

- vcfdist parallelizes by partitioning work, not by sharing mutable state. The Makefile already links `-lpthread`.
- The pattern lives in `dist.cpp::precision_recall_threads_wrapper`: a fixed number of threads (`g.thread_steps[i]`) each take a disjoint slice of superclusters at a given RAM budget (`g.ram_steps[i]`); the main thread `join`s before stepping to the next budget level.
- The default convention is to partition input and write to disjoint output slots, avoiding shared mutable state. `std::atomic` is used where necessary for work-stealing counters and global worker-budget caps (`dist.cpp`, `cluster.cpp`); `std::mutex` and `pthread_mutex_*` are not used. New code that needs cross-thread mutation must justify the choice and document the synchronization model.
- Threading changes require design approval. The design must state the scheduling model, expected scaling benefit, synchronization strategy, and peak-memory effect at the target thread counts.

## Error handling

- Fatal input or invariant failures: print via the `ERROR(...)` / `WARN(...)` macros in `defs.h` and exit with a non-zero status. Match this pattern rather than introducing exceptions in new code unless there is a specific reason.
- HTSlib return codes are checked at call sites; failures surface through `ERROR(...)`.

## Documentation in source

- One short comment line on the `why` is welcome where the intent is not obvious. The existing code has very few comments and most are positional dividers (`/****...****/`) or short noun phrases beside fields (see `src/variant.h`'s vector descriptions).
- No mandatory Doxygen. Header comments that explain a struct's invariants or a vector's meaning (as in `ctgVariants` in `variant.h`) are the bar.
- Do not add file-level "Author / Date / Description" boilerplate — none of the existing files has it.

## Build hygiene

- `make -C src` must succeed with no new warnings under `-Wall -Wextra`.
- Avoid changes that require new compile flags or new linker flags without an entry in the Makefile.
- `src/Makefile` is hand-written and short; keep it that way. Don't introduce CMake, autotools, or generated build files as a side effect of a refactor except as part of an approved port/rewrite slice.

## What is explicitly out of scope for this style guide

- Renaming existing symbols to a different convention.
- Introducing namespaces or an `include/` tree.
- Mandating Doxygen or a particular comment header format.
- Mandating a specific exception strategy where the codebase currently uses early-exit error macros.

If a refactor pass wants to change any of the above, that is a separate proposal, not a coding-guidelines change.
