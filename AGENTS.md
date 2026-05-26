# Agents Brief

This file is the **brief** for the four agent roles that drive vcfdist refactor work. The agents themselves are configured and run by an external orchestrator (Conductor); this repo does not host their runtime. The role definitions live here so that anyone reading the source can see what each agent is responsible for, what artifact it produces, and where that artifact lands.

If you are looking for *how to do a performance refactor slice yourself*, read `docs/refactoring-plan.md` first. The agent roles below are how that work is divided up when running through the orchestrator.

## Phase taxonomy

One taxonomy, used everywhere. Other docs in this harness reference these phase names verbatim — don't introduce new ones.

1. **Discovery** — understand the current code, benchmark evidence, and performance bottleneck; identify a candidate slice.
2. **Design** — decide the shape of the slice, the seams, the speedup hypothesis, and the memory/correctness risk.
3. **Implementation** — make the change.
4. **Verification** — run the correctness and performance gates and confirm.

## Roles

### Explorer

- **Phase:** Discovery.
- **Input:** the current `src/` tree, prior Explorer output, current `docs/architecture.md`, current `docs/benchmark-progress.json`, and any available benchmark notes.
- **Output:** a short Discovery note that names specific files, symbols, line ranges, benchmark observations, and proposed candidate slices. Each candidate names the files it touches, the suspected hot path or scaling bottleneck, the expected speedup mechanism, the memory-risk profile, and an estimated blast radius (files recompiled, lines moved).
- **Where it lands:** the orchestrator stores Explorer output. If this repo grows a `docs/dev/` directory, the merged-into-history version lives there.
- **Selection rule:** performance evidence beats size. A function over **100 lines** is a candidate for decomposition only when decomposition supports measurement, isolation, correctness, or optimization of a hot path.

### Oracle

- **Phase:** Design.
- **Input:** an Explorer Discovery note and the current `docs/refactoring-plan.md`.
- **Output:** a slice-level design note. Picks one candidate, classifies it as pure refactor, performance refactor, optimization, or port/rewrite experiment, names the target seams, states the performance hypothesis, estimates memory risk, lists the benchmark plan, and lists the correctness gate's expected diff (which should be "none — output unchanged").
- **Constraint:** the slice must respect the **Non-goals** in `docs/refactoring-plan.md`. No new abstractions for their own sake; no rename of existing symbols. Algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes require explicit approval before Implementation.
- **Where it lands:** alongside the Explorer note in the orchestrator's record.

### Fixer

- **Phase:** Implementation.
- **Input:** the Oracle design note.
- **Output:** a branch (`refactor/<slice-name>`, `perf/<slice-name>`, or `port/<slice-name>`) with one or more commits, each of which compiles clean under `-Wall -Wextra`, plus benchmark-tracker updates when measurements are produced.
- **Constraint:** behavior preservation per commit unless the approved design explicitly allows a non-final intermediate; small commits over large ones; the conventions in `docs/coding-guidelines.md` describe vcfdist as it is, follow them.
- **Where it lands:** a branch in this repo, opened as a PR against `master`.

### Librarian

- **Phase:** Verification.
- **Input:** the Fixer's branch.
- **Output:** a pass/fail verdict on the correctness and performance gates defined in `docs/refactoring-plan.md`, including output-diff status, wall-clock comparison, peak RSS comparison, and tracker consistency. If pass, the PR is cleared to merge. If fail, the gate's diff, benchmark delta, and failing invocation are recorded.
- **Constraint:** the gates are the gates. The Librarian does not negotiate or override them. If a gate is wrong, that is a change to `refactoring-plan.md` made separately.
- **Where it lands:** a verdict comment on the PR.

## Information flow

```
Explorer → Oracle → Fixer → Librarian → (merge or back to Oracle)
```

If the Librarian fails the gate, the loop returns to Oracle for redesign, not to Fixer for retry. A failed gate means the slice was the wrong shape, not just the wrong commit.

Design decisions are approved before Implementation. Once approved, Fixer and Librarian run the full loop to completion unless the gate fails or the design assumptions are proven wrong.

## Agent Decision Making Protocol

All agents shall follow these principles when making design decisions:

1. **Question-Driven Approach**: Before making any significant architectural calls, benchmark-policy decisions, language boundary decisions (Rust/C++), or workflow/process changes, agents shall ask targeted questions to clarify requirements and constraints rather than making assumptions.

2. **Exception for Routine Tasks**: For trivial mechanical edits or clearly specified implementation steps, agents may proceed without unnecessary questioning.

3. **Collaborative Alignment**: This ensures that all design choices align with user intent and project goals while preventing premature or incorrect architectural decisions.

## What this brief is not

- Not an orchestrator config. The agents are configured in Conductor, not here.
- Not a status-report cadence. Daily/weekly reporting is the orchestrator's concern.
- Not a project plan. Project planning lives in `docs/refactoring-plan.md`, which is the canonical source of goals, non-goals, correctness/performance gates, and the workflow per slice.
- Not a coding style guide. That lives in `docs/coding-guidelines.md`.

## Companion documents

- `docs/architecture.md` — the snapshot of `src/` as it stands. Discovery starts here.
- `docs/refactoring-plan.md` — goals, non-goals, correctness/performance gates, per-slice workflow.
- `docs/coding-guidelines.md` — the conventions observed in `src/`.
- `docs/multiagent-process.md` — the orchestrator-facing process notes that complement this brief.
- `testing.md` — the correctness/performance fixture and benchmark protocol used by the gates.
- `docs/benchmark-progress.json` — machine-readable live tracker for benchmark baselines and slice results.
- `INFRASTRUCTURE_SUMMARY.md` — one-page index into the above.
