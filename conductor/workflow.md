# Workflow

## Operating model

Conductor coordinates performance-refactor slices through the phase taxonomy in `docs/agents/index.md` (`oh-my-opencode-slim` taxonomy). Agent roles and information flow: see `docs/agents/index.md` and `docs/agents/*.md`.

Summary flow:

```text
explorer → designer/oracle → fixer → documentarian → (merge or back to designer/oracle)
```

If a gate fails, control returns to `designer`/`oracle` for redesign — not back to `fixer`.

## Per-slice workflow

1. Branch from `master` using the appropriate prefix.
2. Classify the slice before implementation.
3. Capture baseline correctness and benchmark runs against the documented baseline (see `testing.md § Baseline`); archive the timed benchmark output tree for later diffs.
4. Write a design note with performance hypothesis, correctness risk, memory risk, benchmark plan, and expected output diff (`none`).
5. Get approval before implementation, especially for algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes.
6. Implement in small commits that compile cleanly.
7. Rerun correctness gates on the branch tip and diff against archived baseline outputs.
8. Rerun performance gates, validate the exact timed benchmark output tree against the archived baseline output tree, and compare wall-clock runtime, scaling efficiency, and peak RSS.
9. Update `docs/benchmark-progress.json` with measurements or the enabling-only rationale.
10. Update `docs/architecture.md` if file structure, data flow, concurrency, or language boundaries changed.
11. Merge only after gates and approvals are satisfied.

## Mandatory gates

Correctness gate: see `testing.md § Correctness regression gate`. Build `src/vcfdist` and run `bash demo/regression.sh`; output tree and error counts must be unchanged.

Performance gate: see `testing.md § Baseline`, `testing.md § Timed-output validation`, and `docs/refactoring-plan.md § Performance gate` for the measurement protocol, baseline version, Docker-RSS caveat, thread sweep targets, and memory acceptance rule.

## Non-goals for routine slices

- No correctness shortcuts.
- No public CLI or output-format changes without separate approval.
- No wholesale renaming to a different style.
- No abstractions for their own sake.
- No new external dependency or production runtime without approval.
