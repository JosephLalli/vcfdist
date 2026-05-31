# Agent Map

This repository uses `oh-my-opencode-slim` agent names. The Orchestrator should delegate by these roles when the task benefits from narrower context, independent review, or specialized work.

| Agent | Use when | Do not use for | Reads first | Expected return |
|---|---|---|---|---|
| `orchestrator` | Decomposing work, routing agents, reconciling results | Deep implementation once a task is scoped | `AGENTS.md`, `INFRASTRUCTURE_SUMMARY.md`, this file | Plan, delegation choices, final synthesis |
| `explorer` | Mapping files, finding relevant code paths, summarizing repo structure | Making edits | `INFRASTRUCTURE_SUMMARY.md`, repo tree, relevant docs | File map with confidence notes |
| `librarian` | External docs, package docs, current web/API facts | Local code changes | Cited external sources and relevant repo docs | Source-grounded notes |
| `oracle` | Architecture, refactoring risk, benchmark interpretation, policy changes | Routine implementation | `docs/architecture.md`, `docs/refactoring-plan.md`, `testing.md` | Decision memo, risks, go/no-go |
| `designer` | Interface design, test design, acceptance criteria, implementation shape | Final implementation without review | Architecture, testing, refactoring docs | Design sketch and acceptance criteria |
| `fixer` | Bounded implementation, tests, mechanical edits | Broad unspec'd refactors | Task spec and relevant docs | Patch summary and commands run |
| `council` | Expensive multi-model consensus on high-risk choices | Routine edits | Specific question and relevant source docs | Compared recommendations |
| `observer` | Images, screenshots, PDFs, visual artifacts | Text-only repo search | Supplied artifact plus relevant docs | Structured observations |
| `documentarian` | Documentation drift, stale references, AGENTS/doc consistency, post-change doc alignment | Code implementation or benchmark-policy changes | `AGENTS.md`, `INFRASTRUCTURE_SUMMARY.md`, changed docs, changed code summary | Drift report and minimal doc edits |

## When to invoke `documentarian`

Invoke `documentarian` when a task changes:

- public behavior,
- command-line behavior,
- test expectations,
- benchmark interpretation,
- refactoring policy,
- architecture docs,
- agent behavior or delegation rules,
- `AGENTS.md`,
- `INFRASTRUCTURE_SUMMARY.md`,
- any file under `docs/agents/`.

`documentarian` identifies drift and proposes minimal documentation edits. It must not decide architecture or benchmark policy; escalate those to `oracle` or human review.

## Phase mapping (legacy `Explorer/Oracle/Fixer/Librarian` to slim taxonomy)

The earlier four-role brief (`Explorer → Oracle → Fixer → Librarian`) is preserved as a workflow shape, but the slim taxonomy distinguishes Discovery work (`explorer`) from policy/risk work (`oracle`), and adds a Design step (`designer`) and a Verification step (`fixer` runs the gates; `documentarian` checks docs). Mapping:

| Legacy role | Slim role(s) | Notes |
|---|---|---|
| Explorer (Discovery) | `explorer` | Same scope: code reconnaissance, candidate slice notes. |
| Oracle (Design) | `oracle` + `designer` | `oracle` owns risk/policy; `designer` owns the slice shape and acceptance criteria. |
| Fixer (Implementation) | `fixer` | Same scope: bounded implementation under an approved design. |
| Librarian (Verification) | `fixer` runs the gates; `documentarian` checks doc drift | The slim `librarian` is external-docs only; the old "Librarian" gate role is split: the `fixer` invokes the gate scripts and `documentarian` checks that docs reflect the change. |

The information-flow diagram remains:

```
explorer → designer/oracle → fixer → documentarian → (merge or back to designer/oracle)
```

A failing performance or correctness gate returns control to `designer`/`oracle`, not back to `fixer`.

## Where artifacts live

| Phase | Producer | Artifact | Repo location |
|---|---|---|---|
| Discovery | `explorer` | Candidate-slice note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Design | `designer` (with `oracle` risk review) | Slice design note | Orchestrator record; merged copies optionally under `docs/dev/` |
| Implementation | `fixer` | Refactor/perf/port branch + PR | `refactor/<slice-name>`, `perf/<slice-name>`, or `port/<slice-name>` branch, PR against `master` |
| Verification | `fixer` (gates) + `documentarian` (doc drift) | Gate verdict + doc-alignment note | PR comment; `docs/benchmark-progress.json` updated when measurements are produced |

The `docs/dev/` directory is the user-facing wiki mirror, not a place for internal project tracking. Cross-harness project status (what's been tried, planned, ruled out) lives in `conductor/` and in `docs/refactoring-plan.md § Tried and retired` plus `docs/benchmark-progress.json`.

## Loop boundaries

- **`explorer` → `designer`** is the only handoff that can produce multiple candidate slices. `designer` (with `oracle`'s risk review) picks at most one per cycle.
- **`fixer` ↔ gate verdict** is *not* a tight retry loop. A failing gate returns control to **`designer`/`oracle`**, not `fixer`. The premise is that a failing gate means the slice was the wrong shape, and reshaping is a Design responsibility.
- **Cross-slice coordination** (two refactor branches that touch the same file) is the orchestrator's responsibility. If two slices conflict, the second slice replans against the merged state of the first.
- **Approval before implementation** is mandatory. Design notes require approval before `fixer` starts. Algorithm, threading, memory-layout, runtime/language, dependency, CLI, or output-semantic changes require explicit approval in the design note.
- **Completion after approval** is the default. Once a design is approved, `fixer` runs the full implementation/verification loop (including the correctness and performance gates) to completion unless a gate fails or the design assumptions are proven wrong.

## Gates

Defined in `docs/refactoring-plan.md` under "Correctness gate" and "Performance gate". `fixer` invokes them and reports the verdict; no role can waive them. Changes to a gate are separate document updates, not per-slice decisions.

## Hard constraints (agent process)

- Do not change benchmark thresholds without `oracle` or human review.
- Do not broaden a scoped implementation task without returning to `orchestrator`.
- Do not duplicate canonical policy text; link to the canonical source.
- Do not commit per-harness duplicate instruction files (`COPILOT_INSTRUCTIONS.md`, `GEMINI*.md`, `.cursorrules`, etc. — see `.gitignore`).
- Do not put content in `CLAUDE.md`; it is a pointer file. Edit `AGENTS.md` or the relevant doc instead.
- Keep `AGENTS.md` short; it is a table of contents. Move detail into the relevant doc.
- Keep `INFRASTRUCTURE_SUMMARY.md` as the canonical doc map.

## Per-agent role files

The files in this directory (`orchestrator.md`, `explorer.md`, `librarian.md`, `oracle.md`, `designer.md`, `fixer.md`, `documentarian.md`) are the Codex-harness agent definitions. The Claude harness has its own agent definitions under `~/.claude/agents/` (council, designer, documentarian, fixer, ideas-guy, librarian, observer, oracle) that serve the same roles; Claude agents may ignore the files here as long as the role semantics match. `tools/check_agent_docs.sh` enforces the Codex-side file structure.
