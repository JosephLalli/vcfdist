# Plan: intra-alignment score-wave parallelism for precision/recall

## Status

| Slice | Title | Class | State |
|---|---|---|---|
| 0 | Measure the Amdahl ceiling | exploratory | **DONE — GO** (2026-05-30) |
| 1 | Extract and restructure the dense fill | performance refactor | not started |
| 2 | Thread-safe writes, single-threaded | performance refactor | not started |
| 3 | Turn on intra-alignment threads | optimization | not started |
| 4 | Port the pattern to the corridor | optimization | not started |

Slice 0 result: the longest dense alignment of the largest `HG00733_chr2_full`
supercluster has a **63.88x parallel ceiling at 64 threads** with zero thin tails.
This is a strong GO. Details in [Slice 0 results](#slice-0-results-measured) and in
`docs/benchmark-progress.json` (`slice_results[].name == "pr_wavefront_parallel_slice0"`).

## Why this exists

The precision/recall stage is the runtime long pole on `HG00733_chr2_full`, and
within it the floor is a single supercluster's single alignment. Superclusters are already processed in parallel through a shared `PrThreadPool`
(`g_pr_pool`, `dist.cpp`), and each supercluster's four query/truth haplotype
alignments are submitted as pool tasks and dispatched to pool workers. So a "giant"
supercluster maxes out at four cores while the rest of a 64-core machine idles, and the
wall-clock floor is one serial alignment of the biggest supercluster
(`calc_prec_recall_aln`, `dist.cpp`). This is the visible cause of the thread-
utilization sawtooth: many small superclusters saturate all cores, then a giant drops
effective utilization toward four until it finishes, then the next batch saturates
again.

This plan parallelizes that single alignment from the inside, without splitting the
supercluster. Anchor-splitting the giant is explicitly rejected: a supercluster is by
construction the minimal set of variants that are mutually dependent on each other's
alignment. If it could be cut at safe anchors, the clustering step would already have
cut it. Score-wave parallelism keeps the alignment whole and exploits parallelism that
already exists inside the dynamic program.

**Why this and not the corridor.** The shipped default WFA corridor already speeds up
large alignments ~3x, but that speed is an *approximation discount*: its savings come
entirely from marking cells done-on-enqueue (processing only the first predecessor per
cell), which is exactly the work whose absence undercounts true-positive credit by 3
INDEL TPs on full-chr2. A byte-exact corridor is no faster than the plain dense state
matrix (measured 45.6s vs 44.0s PR stage). Intra-alignment parallelism is the
**accuracy-neutral** alternative: it speeds up the *exact* fill (and, in Slice 4, the
corridor) by using more cores, not by approximating. It is additive with the byte-exact
state-matrix win (`33dea6b`, 1.86x) and does not touch the corridor's accuracy tradeoff.

This plan is governed by `docs/refactoring-plan.md`. Its correctness gate, performance
gate, thread sweep, memory cap (per-core RSS < 3.0x baseline), and per-slice workflow
apply here unchanged. This document adds the feature-specific staging, the measured
ceiling, the threading-integration design, and the risks unique to it.

## Slice 0 results (measured)

Instrumented the dense fill behind `VCFDIST_PR_WAVE_DUMP=1` (throwaway, gated; remove
after this go/no-go). The run records, for the longest dense alignment of the largest
full-chr2 supercluster, the per-wave frontier widths and totals. Because windowed
fixtures never build a giant supercluster or trip the 1M-cell corridor threshold, this
was measured on `HG00733_chr2_full` under `--exact-prec-recall` (so the giant falls
through to the dense path the instrumentation reads).

Giant alignment geometry:

| Quantity | Value |
|---|---|
| query_len x truth_len | 4557 x 6396 |
| cell_count (query_len*truth_len) | 29,146,572 |
| waves `S` (score barriers) | 1,858 |
| cells processed `N` (sum of wave widths) | 29,953,339 |
| avg / median wave width | 16,121 / 16,379 |
| min / max wave width | 2,224 / 20,683 |
| waves narrower than the 64-thread team | 0 |

Amdahl ceiling `N / sum_w ceil(width_w / P)`:

| P | 4 | 8 | 16 | 32 | 64 |
|---|---|---|---|---|---|
| ceiling | 4.00x | 8.00x | 15.99x | 31.97x | **63.88x** |

**Go decision rationale.** The waves are fat and uniform (avg 16,121 cells, p10 14,618,
p90 19,030), so per-thread work per wave at P=64 is ~250 cells — well above any
plausible barrier/pool overhead. There are no thin tails (the narrowest wave is 2,224,
~35x the team size), so the reachable diamond is wide from start to finish. The only
serial cost is `S = 1858` barriers; at a few microseconds per 64-thread barrier that is
single-digit milliseconds against a multi-second alignment. The ceiling is therefore
near-perfect and the speedup is accuracy-neutral. **GO.**

## The parallel structure that already exists in the algorithm

The forward pass in `calc_prec_recall_aln` is a breadth-first search that is
**level-synchronous by edit score**. Each iteration of the outer `while` loop:

1. EXTEND: follows free (match / swap) transitions at the current score, an inner BFS
   whose frontier grows by chained diagonal matches.
2. Marks every cell reached at this score `PR_DONE` — **at the wave barrier**, after the
   whole wave is processed, not on first reach.
3. NEXT SCORE: expands INS / DEL / SUB transitions from the previous wave to seed score
   `s+1`.

Wave `s+1` depends only on wave `s`. Within a wave, cells are mutually independent in
their reads (they read the prior wave's `PR_DONE` state) and only write pointer bits
into successor cells. That independence is the parallelism. Pointer bits are
OR-accumulated (`ptrs[hi][qri][ti] |= PTR_*`), and a union of bits is order-independent,
so the forward DAG is byte-stable under any thread order **as long as** every co-optimal
predecessor that would set a bit serially still sets it in parallel — guaranteed by
marking `PR_DONE` only at the wave barrier (the property the corridor breaks for speed
and this plan preserves).

The backward pass `calc_prec_recall_path` is the max-true-positive dynamic program that
consumes this DAG; it is unchanged by this work. This plan parallelizes only the forward
fill, exact and corridor alike, and never changes which DAG the backward pass sees.

## The three existing parallelism levels and how the new one nests

Today the PR stage has two nested levels of work routed through a single pool; this
plan adds fine-grained intra-alignment parallelism as additional pool tasks. All task
budgets must stay within the fixed `PrThreadPool` of `max_threads - 1` workers.

1. **Outer (supercluster partition).** `precision_recall_threads_wrapper`
   (`dist.cpp`) dispatches all superclusters through `g_pr_pool` regardless of RAM
   group; the calling thread participates as an extra worker. The previous per-tier
   `nthreads` and `thread_step` dispatch are replaced by a single shared pool.
2. **Four-haplotype split.** Within one supercluster, `precision_recall_wrapper`
   (`dist.cpp`) submits `CALLSETS*HAPS = 4` alignment tasks to `g_pr_pool`; the
   dispatching thread spin-steals from the same queue to avoid blocking. This replaced
   the previous `run_threaded_alignments` + reservation system.
3. **NEW — intra-alignment (this plan).** Parallelize the frontier inside a single
   `calc_prec_recall_aln` alignment for giant-class alignments only.

**Budget interaction (the headline integration risk).** A giant supercluster reaches the
fill through the four-haplotype split, so when intra-alignment threads turn on there are
two live multipliers: 4 alignments x W intra-workers. Two integration shapes, to be
chosen by measurement in Slice 3:

- **A — keep the 4-way split, cap intra width at `max_threads/4`.** Each of the 4
  alignments runs concurrently with a `W = max_threads/4` team (16 at 64 threads). Total
  = `4 x 16 = 64`. Simplest; reuses the existing reservation. Wall on the giant is
  `max_i(T_i)/16` where `T_i` are the four alignment costs.
- **B — serialize the 4 alignments, give each the full team.** Each alignment runs with
  `W = max_threads` (64-wide), one after another. Total = 64. Wall is `sum_i(T_i)/63.88`.
  Better load balancing when the four costs are imbalanced; loses 4-way overlap.

For roughly equal alignment costs A and B are within noise (both ~`T/16`). Start with A
because it reuses the existing structure and reservation; measure both if the four costs
are imbalanced. Either way, the intra-alignment team must draw from the *same* global
budget as the 4-way split so the three levels never sum above `max_threads`: extend the
reservation to account for intra-workers, or compute `W` from
`max_threads - (active outer threads) - (4-way workers)` at entry.

## The crux: determinism of `swap_pred_map`

One write in the forward pass is **not** an order-independent union:
`swap_pred_maps[i][z] = x` (an `unordered_map<idx1, idx1>`) is last-writer-wins. Serially
the winner is the last `x` in BFS order; under threads it is nondeterministic. Byte-
exactness under threads is the entire advantage of this approach over the corridor, so
this write must be made deterministic.

Chosen design: pick a **canonical** winner independent of thread order — the predecessor
`x` with the smallest key under a fixed total order on `idx1`'s `(hi, qri, ti)` triple —
implemented as a per-cell atomic-min on an encoded predecessor key. Concretely, pack
`(qri, ti)` (and `hi` if a cell can be reached across haplotype indices) into a single
unsigned key, keep a parallel `swap_pred_key[z]` array initialized to `UINT_MAX`, and on
each candidate predecessor do `atomic_min(swap_pred_key[z], encode(x))`; after the wave
barrier, materialize `swap_pred_maps[i][z]` from the winning key.

Whether the canonical winner reproduces the serial last-writer result byte-for-byte on
full-chr2 is the **first thing Slice 2 must prove**. If it does, threads are free. If it
does not, we either (a) replicate serial BFS order deterministically (a stable per-cell
tie-break that mirrors enqueue order), or (b) accept a documented deterministic
difference per the byte-exact tradeoff policy in `docs/refactoring-plan.md`. That is a
human decision, surfaced before Slice 3 with the measured diff in hand — not a silent
choice.

## Slices

Each slice is independently gated and mergeable. The ordering de-risks: prove the
ceiling (done), restructure with no behavior change, make writes safe single-threaded,
then turn threads on. Full-chr2 is mandatory at every gate because windowed fixtures
never build a giant supercluster and never trip the 1M-cell corridor threshold.

### Slice 0 — measure the Amdahl ceiling (exploratory) — DONE, GO

Recorded above and in `docs/benchmark-progress.json`. Instrumentation
(`VCFDIST_PR_WAVE_DUMP`, `pr_wave_widths.csv`) is throwaway and must be removed during
Slice 1 (it is currently uncommitted in `src/dist.cpp`).

### Slice 1 — extract and restructure the dense fill (performance refactor, byte-exact)

- **Goal:** expose the parallel boundary with zero behavior change.
- **Do:** extract the inline dense BFS from `calc_prec_recall_aln` into its own
  `calc_prec_recall_aln_one_dense(...)` (mirroring the existing
  `calc_prec_recall_aln_one_wfa` factoring at `dist.cpp:444`), then rewrite its loop so
  the frontier is explicit `current` / `next` index vectors over the flat state matrix
  and the body is a pure "process frontier -> produce next frontier" step. No
  `std::queue`. Still single-threaded. Remove the Slice-0 instrumentation in the same
  slice.
- **Gate:** `bash demo/regression.sh` matches `demo/results/`; full-chr2 in
  `--exact-prec-recall` byte-identical to the golden via `tools/compare_vcfdist_runs.py`
  (see `testing.md § Canonical timed command` for the exact-mode note). Perf neutral on
  full-chr2 (pure/performance refactor — must not materially slow the benchmark).
- **Branch:** `perf/pr-wavefront-restructure`.

### Slice 2 — thread-safe writes, run single-threaded (performance refactor, byte-exact)

- **Goal:** isolate "did I break correctness converting writes" from "did threading
  introduce a race" by making the writes safe but executing on one thread.
- **Do:**
  - `ptrs[hi][qri][ti] |= bit` becomes `__atomic_fetch_or(&cell, bit, __ATOMIC_RELAXED)`
    on the existing `uint8_t` cells (GCC builtin; no `Globals`/type ABI churn; C++17-safe).
  - the `PR_NEW -> PR_IN_WAVE` transition becomes a CAS so each cell enqueues exactly once.
  - `swap_pred_map` gets the canonical atomic-min winner from the crux section above.
- **Gate:** single-threaded run byte-identical to Slice 1 on demo + full-chr2 exact. This
  is also the **`swap_pred_map` determinism proof**: compare the canonical-winner output
  to the serial golden and record the result. This slice deliberately breaks the
  codebase's disjoint-partition threading invariant (`refactoring-plan.md`, "What can go
  wrong"); the design note must justify the synchronization explicitly.
- **Branch:** `perf/pr-wavefront-atomics`.

### Slice 3 — turn on intra-alignment threads for giant alignments (optimization)

- **Goal:** the actual speedup.
- **Do:** wrap the whole single-alignment fill in **one persistent parallel team**
  (`#pragma omp parallel` spanning the alignment) with an internal barrier between waves.
  Do **not** spawn threads per wave — thousands of waves x thread launches would dominate.
  Parallelize the frontier processing across the team (static or guided split of the
  `current` frontier; each thread expands its slice and appends to a per-thread `next`
  buffer merged at the barrier). Gate intra-alignment threading on a cell-count /
  wave-width threshold (start at the corridor threshold, `1,000,000` cells, or a higher
  bar tuned so only giant-class alignments pay team-setup cost; small alignments stay
  serial). Integrate with the worker budget per the [budget interaction](#the-three-existing-parallelism-levels-and-how-the-new-one-nests)
  section so outer + 4-way + intra never oversubscribe `max_threads`; implement shape A
  first.
- **Gate:** correctness **byte-identical between 1 and N threads** on full-chr2 exact
  (the determinism proof under real concurrency); performance thread sweep
  `1, 2, 8, 16, 32, 64` recording PR-stage wall, total wall, and peak RSS; per-core RSS <
  3.0x baseline. Report effective speedup against the Slice 0 ceiling.
- **Kill switch:** keep an env/threshold gate (e.g. `VCFDIST_PR_INTRA_THREADS` or the
  cell threshold) so the feature can be disabled without a revert if the sweep regresses.
- **Branch:** `perf/pr-wavefront-parallel`.

### Slice 4 — port the proven pattern to the corridor (optimization)

- **Goal:** speed up the **default** path, not just `--exact-prec-recall`.
- **Why separate:** the corridor is the default for giants, so Slices 1-3 (dense fill)
  only help exact-mode wall clock. The corridor's Phase-1 sweep and Phase-2 fill in
  `calc_prec_recall_aln_one_wfa` have the same score-wave structure, so the same
  restructure + atomics + team pattern ports over. Dense first because its
  byte-exact-vs-golden gate is unambiguous; corridor second.
- **Gate:** corridor default byte-identical between 1 thread and N threads (determinism of
  the approximation — the corridor must remain the *same deterministic* approximation, not
  a thread-order-dependent one); performance thread sweep on full-chr2.
- **Branch:** `perf/pr-wavefront-corridor-parallel`.

## Thread-sweep and acceptance protocol

Use the canonical timed command in `testing.md § Canonical timed command` on
`fixtures/HG00733_chr2_full`. For Slices 3-4, run the sweep `THREADS in {1, 2, 8, 16, 32,
64}` (optional stretch `128, 256` on a NUMA host) and record per run, in
`docs/benchmark-progress.json`:

- PR-stage seconds and total wall clock;
- peak RSS (and per-core RSS = peak/threads vs baseline; cap 3.0x);
- output-diff verdict from `tools/compare_vcfdist_runs.py` (exact mode for byte-compare);
- the realized speedup vs the Slice 0 ceiling at that thread count.

Acceptance: an optimization slice must show measured PR-stage / scaling improvement at the
relevant thread counts with no correctness regression (byte-identical in exact mode for
Slices 1-3; deterministic-and-unchanged corridor output for Slice 4) and per-core RSS
within 3.0x.

## Risk register

- **`swap_pred_map` determinism (headline).** If the canonical winner does not reproduce
  serial output, byte-exactness under threads is at risk. Resolved or escalated in Slice 2
  before any threads turn on.
- **Oversubscription (headline).** Three nested levels of parallelism (outer x 4-way x
  intra). The worker budget must keep the product at or below `max_threads`; otherwise the
  64-core sweep regresses. Mitigated by drawing intra-workers from the same global
  reservation and gating intra-threading to giant-class alignments only.
- **Amdahl from thin tails and barriers.** Slice 0 retired this for the measured giant
  (zero thin tails, 1858 cheap barriers), but a different fixture or a smaller "giant"
  could have a worse profile; the Slice 3 sweep is the live check.
- **Thread-launch cost.** One persistent team per alignment with internal barriers, never
  a team per wave. The per-thread `next` buffers must be reused across waves, not
  reallocated each barrier.
- **Memory.** The matrices are shared, not duplicated, so per-core RSS impact should be
  small, but per-thread `next` frontier buffers and thread stacks add some. Peak RSS is
  the hard gate on full-chr2 and headroom is thin — peak is the global retained-data floor
  (`project-full-chr2-rss-floor`), not the matrices — so measure peak every slice.
- **Optimizer / inlining.** Extracting the dense fill (Slice 1) can change inlining under
  `-O3`; the perf-neutral full-chr2 gate catches a regression there.
- **Interaction with the corridor default.** Slices 1-3 only exercise the dense path,
  which is off by default. Without Slice 4 the *default* giant wall clock is unchanged;
  decide before Slice 3 whether exact-mode speedup alone is worth shipping or Slice 4 is
  required (see open decisions).

## Open decisions

1. **Canonical `swap_pred_map` winner vs. exact serial-order replication** — decided by
   the Slice 2 byte-compare result.
2. **Budget shape A vs B** for the 4-way x intra interaction — decided by the Slice 3
   sweep and whether the four alignment costs are imbalanced.
3. **Ship exact-mode parallelism (stop at Slice 3) or push to the corridor default (Slice
   4)** — depends on whether the target is exact-mode reproducibility wall clock or the
   default giant wall clock. If the corridor accuracy tradeoff is later rejected in favor
   of exact-as-default, Slices 1-3 become the primary speedup and Slice 4 is moot.

## Validation fixtures

- `bash demo/regression.sh` vs `demo/results/` — cheap correctness, every commit.
- Windowed fixtures (chr2 window, chr22 phaseflip) — smoke only; they do not build a giant
  supercluster and never trip the corridor threshold, so they cannot gate any slice here.
- `fixtures/HG00733_chr2_full` via `tools/compare_vcfdist_runs.py` — the only fixture that
  exercises the giant, the corridor, and the perf/RSS gates. Mandatory (exact mode for
  byte-compare) for every correctness and performance gate in this plan.
