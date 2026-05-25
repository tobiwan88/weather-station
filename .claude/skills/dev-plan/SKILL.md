---
name: dev-plan
description: Use when the user asks to create an implementation plan, decompose a feature into subtasks, or produce a development plan for a weather-station feature. Produces file-allowlisted subtasks for parallel execution. Does not write code.
argument-hint: "[REQ-file-path or feature description]"
allowed-tools: Read, Write, Bash, Glob, Grep
---

# Decompose a Feature into Implementation Subtasks

Use this skill to **plan** a feature by decomposing it into independent,
file-allowlisted subtasks that can execute in parallel. This agent does NOT
write code — it only produces the plan.

The plan is consumed by `/dev-coordinate` (future) to dispatch each subtask.

**Argument received** (optional): `$0` — a REQ file path or free-text feature description.

---

## Step 0 — Understand the feature

Read `$0`. If it's a file path, read the file. If it's free-text, use it directly.

Before building the plan, identify **ambiguities** and **missing constraints**:

- Power, latency, memory budgets
- API / protocol choices not specified
- Module boundaries (existing lib vs. new lib)
- Error handling, edge cases, timing, initialization order
- Conflicts with existing ADR constraints (check `docs/architecture/architecture-constraints.md`)

Ask clarifying questions for anything underspecified. Ask **all questions in ONE message**.
Do not produce a plan until questions are answered.

---

## Step 1 — Load context

Read these in parallel:

1. The REQ file (if `$0` is a path)
2. `docs/architecture/architecture-constraints.md` — ADR-derived implementation constraints
3. All ADRs referenced from the REQ — read the full `.md` file from `docs/adr/`
4. `docs/architecture/README.md` — jump to relevant architecture docs for the feature domain
5. `CLAUDE.md` — coding conventions, banned patterns, library catalog
6. Search `lib/` for existing code in the same domain (e.g., for a new sensor driver,
   search for existing I2C or fake sensor patterns to follow)
7. Search `dts/bindings/` for relevant DTS bindings

---

## Step 2 — Decompose into independent subtasks

A subtask is **independent** if it meets **all** of:

- Touches **different files** from other subtasks
- Has **no compile-time dependency** on another subtask's output
- Has no runtime dependency (it can be written/coded independently — runtime integration
  is verified later)

Each subtask must:
- Touch **1–5 files** max
- Have a **single clear responsibility**
- Be completable in **one agent session**

For each subtask, produce a YAML block. See [`references/subtask-yaml-format`](references/subtask-yaml-format) for the complete format specification and rules.

**Rules:**
- New files → `files_to_create`. Modifications to existing files → `files_to_modify`.
- Unit tests (ztest) **may** be included in `files_to_create` alongside the code they test.
- Integration/E2E/system-level tests (pytest, SIL, HIL) are **never** in either field —
  they have a separate workflow.
- If the feature requires a **new zbus channel**, flag it explicitly — this needs an ADR.
- Keep subtask count **≤ 5** for a typical REQ.
- File allowlists are **hard boundaries**. If a subtask needs a file not listed,
  the plan must be revised.

---

## Step 3 — Generate dependency graph

Produce a text-based graph:

```
IMPL-001 (new module + unit tests) ──┐
IMPL-002 (DT binding)               ──┼──► IMPL-004 (integration test)
IMPL-003 (Kconfig wiring)            ──┘
```

- `──►` means "must wait for"
- `──` means "can run in parallel with the adjacent node(s)"

---

## Step 4 — Flag file conflicts

List any files touched by more than one subtask. These **must** become sequential
dependencies or the plan must be reshuffled. Include the conflicting subtask IDs
and the shared file path.

If no files are touched by more than one subtask: "**No conflicts.**"

---

## Step 5 — Present the draft for discussion

Present the complete plan using the template in [`references/plan-template`](references/plan-template). Do **not** write to disk yet.

Ask:
> "Does this plan look right? Anything to add, remove, reorder, or clarify before I
> write it to disk?"

Incorporate changes. Repeat until the user confirms.

---

## Step 6 — Write the plan file

Create the `docs/plans/` directory if it doesn't exist:

```bash
mkdir -p docs/plans
```

Derive a **kebab-case feature slug** from the title. Use only lowercase letters, digits,
and hyphens.

File path: `docs/plans/impl-plan-<feature-slug>-YYYY-MM-DD.md`

Write the confirmed draft to disk.

---

## Step 7 — Notify the user

Report the written plan path and suggest next steps:

> "Plan written to `docs/plans/impl-plan-<slug>-YYYY-MM-DD.md`. When ready,
> `/dev-coordinate` will dispatch each subtask. Run `/build-and-test` after each
> subtask completes."

---

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "This is simple, I can plan it in my head" | Without a written plan, subtasks will overlap files and cause merge pain. Write it. |
| "All subtasks are sequential anyway" | Almost always false. Look harder for independent slices (DT binding vs. test vs. Kconfig). |
| "I'll figure out the files as I go" | File allowlists are the core output. Without them, the plan is useless. |
| "7 subtasks is fine, it's a big REQ" | A 7-subtask REQ contains hidden coupling. Split into two plans. |
| "This feature doesn't need an ADR" | If it introduces a new channel, pattern, or library type, it likely does. Check the threshold rules. |

## Next steps

After the plan is confirmed and written:
- If a new zbus channel or pattern is introduced → invoke `/adr` before implementation
- When ready to implement → follow `/build-and-test` after each subtask
