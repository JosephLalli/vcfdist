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
