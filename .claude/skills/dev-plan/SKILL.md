---
name: dev-plan
description: Use when the user asks for an implementation plan, task breakdown or development plan for a weather-station feature. Writes a checkbox plan to docs/plans/ with file-allowlisted subtasks for implementer agents. Writes no code.
argument-hint: "[REQ-file-path or feature description]"
allowed-tools: Read, Write, Bash, Glob, Grep
---

# Plan a Feature as File-Allowlisted Subtasks

Plan only. Do not write code. Output: `docs/plans/YYYY-MM-DD-<slug>.md`, shaped by
[`references/plan-template.md`](references/plan-template.md).

**Argument** (optional): `$0`, a REQ file path or a free-text description.

## 1. Clarify

Read `$0` if it is a path. Find gaps: budgets (power, latency, memory), protocol choices,
module boundaries (existing lib or new lib), error handling, init order. Ask all questions
in ONE message and wait for answers before planning.

## 2. Read the code

Ground the plan in the repo, not memory:

- `lib/` and `apps/`: existing code in the same domain; follow its Kconfig, SYS_INIT and zbus pattern.
- `dts/bindings/`, `boards/`: DT nodes and overlays. `tests/`: the suite each subtask will run.
- `CLAUDE.md`: banned patterns and the channel list. `docs/backlog.md` if the feature touches a deferred item.
- `docs/decisions.md` for design decisions that constrain the change.

## 3. Decompose

A subtask is independent when no other subtask touches its files and it has no compile-time
dependency on another's output. Each subtask:

- Touches 1–5 files, has one goal, and fits one implementer session.
- Lists its files exactly. The allowlist is a hard boundary; needing another file means revising the plan.
- Puts ztest unit tests in its own files. Pytest, SIL and HIL tests are never in a subtask.
- Has an acceptance command run through `scripts/ws.sh`, for example:
  - build: `scripts/ws.sh 'west build -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'`
  - suite: `scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/<module> --inline-logs -v -N'`
- Is self-contained, because the implementer receives only that subtask block.
- Keeps the plan to ≤ 5 subtasks. A new zbus channel needs a short entry in `docs/decisions.md` first.

Draw a dependency graph (`──►` waits for, `──` runs in parallel). List every file touched by
more than one subtask; each one forces a dependency or a reshuffle. Otherwise write "No conflicts".

## 4. Review the draft

Show the full plan in chat and ask: "Anything to add, remove, reorder or clarify before I
write it to disk?" Do not write before confirmation.

## 5. Write the plan

```bash
mkdir -p docs/plans && date +%F
```

File: `docs/plans/YYYY-MM-DD-<slug>.md`, with `<slug>` in lowercase letters, digits and hyphens.
Use `- [ ]` checkboxes on every subtask and done-criterion so progress can be ticked in place.

## 6. Hand off

Report the path. Run each subtask in this loop:

1. Give the subtask block to the `implementer` agent (Haiku). It edits only the listed files and runs the acceptance command.
2. Run the acceptance command yourself and confirm the result.
3. Give the diff to the `reviewer` agent (Sonnet, read-only).
4. Fix the findings, re-run the acceptance command, then tick the checkbox.

After all subtasks pass, run `/build-and-test` and commit per CLAUDE.md.
