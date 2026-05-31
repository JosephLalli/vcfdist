# Golden Principles

Core invariants for vcfdist development. Sources: `docs/coding-guidelines.md` and `AGENTS.md` hard constraints.

## Correctness

- Output files, genotyping error rates, switch error rates, and flip error rates must not change unless a separate approved feature proposal explicitly changes public semantics.
- No branch merges until the demo regression gate is green.

## Performance

- **Profile before optimizing.** No optimization or performance refactor starts without a baseline profile (`perf` / flamegraph / callgrind) that pins the hot path at function+line granularity and a whole-program payoff bound ("best case if this part goes to zero"). Instrumenting the inputs to a cost model (cell counts, wave widths) is not profiling. If the payoff is small, stop before designing.
- **Spike before plan.** Prototype the riskiest assumption (usually "will this actually be faster?") and run an isolation sweep on a small fixture before writing the production plan. Any parallelism ceiling must model the design's serial join cost (per-iteration merge/reduce/barrier), not just the problem's latent parallelism, and must be calibrated against at least one real measurement. See `refactoring-plan.md § Workflow per slice`.
- Optimize for high-core wall-clock runtime first (64-core scaling is the near-term target).
- Per-core memory growth must stay below `3.0x` relative to baseline at the same thread count (cap relaxed from `1.3x` on 2026-05-28; rationale in `testing.md`).
- Do not trade correctness for speed.

## Code

- C++17; compile clean under `-Wall -Wextra -O3`.
- No raw `new` / `delete` in new code; no pointer-heavy containers in inner loops without measurement.
- Partition input for parallelism; do not share mutable state across threads.
- New external dependencies or language runtimes require explicit design approval.
- `make -C src` must produce no new warnings.

## Scope and process

- For process and scope constraints (orchestrator escalation, no-duplication policy, CLAUDE.md pointer rule, attribution rule), see `../AGENTS.md` §Hard constraints — the canonical source.
- Threading and Rust-port changes require design approval before implementation.
