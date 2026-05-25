---
name: adr
description: Use when the user adds a new feature, makes an architectural decision, chooses a library/pattern/approach, or says "write an ADR", "add ADR", "document this decision", "record this decision", or "architecture decision record". Automatically invoked after implementing significant features or design choices.
argument-hint: "[decision topic or description]"
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Draft and Discuss an Architecture Decision Record

Use this skill to **draft and discuss** a new ADR. The process is conversational:
ask before writing, confirm before saving.

**Argument received** (optional): `$0` — a brief description of the decision topic.

---

## ADR threshold check

Before proceeding, apply the threshold rule from `docs/adr/README.md`.

**Write an ADR for:**
- A new zbus channel or inter-library communication pattern
- A new Kconfig composition rule
- A new test harness or testing approach
- A significant trade-off with long-term consequences (security posture, deployment
  model, protocol choice, external dependency)

**Skip an ADR for:**
- Helper functions, naming choices, minor config defaults
- Routine refactors or file reorganisations
- Decisions fully captured in a code comment or architecture doc

If the decision is below the threshold, tell the user:
> "This doesn't meet the ADR threshold. It's better documented as a code comment
> or in the relevant architecture doc. Here's why: [reason]."

Stop here if below threshold.

---

## Step 1 — Determine the next ADR number

```bash
ls docs/adr/ADR-*.md | sort | tail -5
```

Extract the highest existing number and add 1. If `ADR-015` is the highest, the
new ADR is `ADR-016`.

---

## Step 2 — Check for superseded ADRs

If the new decision supersedes an existing one, identify it now:

```bash
cat docs/adr/README.md
```

Note the ADR to supersede (if any). It will need its Status updated in Step 5.

---

## Step 3 — Gather context through discussion

Ask the user these questions **one section at a time**. Wait for each answer
before moving to the next. Do not ask all questions at once.

### 3a. Context

Ask:
> "What problem or constraint made this decision necessary? What forces were at
> play — technical constraints, project goals, architectural pressures, risks
> that exist without this decision?"

If the answer describes HOW something was built rather than WHY a decision was
needed, prompt: "That sounds like the implementation. What was the underlying
problem that forced a choice?"

The Context section contains forces and constraints only. No code, no config.

### 3b. Decision

Ask:
> "What was decided? State the choice in one or two sentences — the architectural
> option chosen and the key reason for choosing it over the alternatives."

If the answer includes code snippets, byte formats, config keys, struct layouts,
or directory listings, say: "That level of detail belongs in the architecture
doc or header file — the ADR just needs the choice and the reasoning. Where would
that implementation detail live?"

### 3c. Consequences

Ask:
> "What does this decision make easier? What does it make harder or more
> expensive? What is now constrained or locked in as a result?"

Organise the answer into three buckets: Easier, Harder, Constrained.
If the user gives only positives, probe: "What does this make harder or constrain?"

### 3d. Alternatives considered

Ask:
> "What other options did you consider and reject? For each: what was it, and
> why was it not chosen?"

At least two alternatives are expected. If the user can only think of one,
prompt: "What would the most obvious simpler/heavier/existing-pattern approach
have been?"

### 3e. See also

Ask:
> "Which existing ADRs does this relate to? And where will the implementation
> details live — which architecture doc or header file?"

This produces the "See also" section and confirms the ADR will not become a
reference document for WHAT, only WHY.

---

## Step 4 — Present the draft for discussion

Present the complete draft in the canonical format. See [`references/adr-template.md`](references/adr-template.md) for the full template structure. Do NOT write to disk yet.

Ask:
> "Does this capture the decision accurately? Anything to add, remove, or
> reword before I write it to disk?"

Incorporate any changes. Repeat until the user confirms.

---

## Step 5 — Write the ADR file

Derive a kebab-case filename from the title. Use only lowercase letters, digits,
and hyphens. No special characters.

File path: `docs/adr/ADR-NNN-<kebab-title>.md`

Write the confirmed draft to disk.

**Validate:** Read the file back and confirm the Status, Date, and Decision fields are present and correct.

---

## Step 6 — Update the ADR index

Edit `docs/adr/README.md`. Add a new row to the index table in numerical order:

```markdown
| [ADR-NNN](ADR-NNN-<kebab-title>.md) | Title | Proposed |
```

If this ADR supersedes an existing one, update that row's Status column:

```markdown
| [ADR-0XX](ADR-0XX-old-title.md) | Old Title | Superseded by ADR-NNN |
```

Also update the superseded ADR's own header table Status field.

---

## Step 7 — Add rationale callout to affected architecture docs

If the decision is described by an existing architecture doc, add or update the
design-rationale callout near the top of that file:

```markdown
> Design rationale: [ADR-NNN](../adr/ADR-NNN-<kebab-title>.md), ...
```

If no architecture doc exists yet for this decision area, note it to the user:
> "There's no architecture doc for [topic] yet. The See also section points to
> [lib/path] for now. Consider creating `docs/architecture/<topic>.md` when the
> implementation is complete."

---

## Step 8 — Regenerate architecture constraints

Invoke `/arch-sync` to update `docs/architecture/architecture-constraints.md`
with the new ADR's constraints.

**Terminal state:** `docs/adr/ADR-NNN-<kebab-title>.md` written, `docs/adr/README.md` updated, relevant architecture docs have rationale callout, `docs/architecture/architecture-constraints.md` regenerated.

---

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "This doesn't need an ADR" | Check the threshold rules above. New channels, patterns, or library types likely do. |
| "I'll just include the implementation details" | Redirect to architecture doc / header file. ADR = WHY, not HOW. |
| "I can mark it Accepted myself" | Status starts as Proposed. The user or team reviews. |
| "The index update can wait" | Do it now. Stale indexes cause duplicate numbers. |

## Rules

- **No implementation details in the ADR.** If the user wants to include struct
  definitions, config file contents, directory trees, code snippets, command
  examples, or byte-format tables — redirect: "That belongs in the architecture
  doc / header file. The ADR just needs the architectural choice and the reasoning.
  Add it to 'See also' instead."

- **Context = forces and constraints.** Not "here is what we built." If the Context
  sounds like implementation description, ask what problem made the decision necessary.

- **Decision = WHY chosen + WHAT was chosen.** Not HOW it is implemented.

- **ADRs are frozen once Accepted.** If the user is attempting to update the
  Context, Decision, or Consequences of an existing Accepted ADR, stop and redirect:
  > "ADRs are frozen after Accepted. Create a new ADR that supersedes this one
  > instead, and update the old ADR's Status to 'Superseded by ADR-NNN'."

- **Status starts as Proposed.** The user or team reviews and changes it to
  Accepted. Do not mark it Accepted without explicit instruction.

- **Date** is today's date in `YYYY-MM-DD` format.
