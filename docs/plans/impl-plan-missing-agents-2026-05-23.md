# Implementation Plan — Missing Agent Pipeline Components
> Generated from Phase 1 gap analysis of the Agentic Firmware Development Pipeline
> Plan date: 2026-05-23

## Context

Three agents are built (`/arch-sync`, `/standards-check`, `/dev-plan`). Five agents and one skill remain to complete Phase 1. This plan orders them by dependency and value to the weather-station project.

**FFF removed:** The project uses zero FFF mocks. All 7 existing test suites follow the native_sim + zbus-listener + DT-defined fake sensors pattern. Sub-agents find this pattern via cocoindex search. No FFF skill needed.

## ADR Constraints Applicable

| ADR | Constraint | Impact |
|---|---|---|
| ADR-008 | Kconfig-only composition; library CMakeLists only `CONFIG_` gates | `/dev-implement-subtask` must respect this |
| ADR-002 | zbus channel ownership rules | `/test-write` and `/dev-implement-subtask` must follow |
| ADR-003 | Flat struct, no heap pointers | All code-writing agents |
| ADR-009 | native_sim first | Tests target native_sim |
| ADR-012 | Pytest + Twister; no pytest config files | `/test-write` output format |
| ADR-005 | Fake sensors UIDs 0x0001–0x00FF | Implement agents must not reuse real UID ranges |

---

## Subtasks

### IMPL-001: `ask_until_understood` skill
**Domain:** skill
**Files:**
- Create: `.claude/skills/ask-until-understood/SKILL.md`
- Modify: `.claude/skills/arch-sync/SKILL.md` (add ask_until_understood directive)
- Modify: `.claude/skills/dev-plan/SKILL.md` (add ask_until_understood directive)

**Depends on:** none
**Parallel with:** none (first item, nothing to parallelize with)
**Estimated:** 40 lines (+ 6 lines per agent wired in)

**Description:**
Reusable prompt fragment (~30 lines) that agents compose into their system prompt. Enforces: identify gaps, ask all questions in one message, never invent missing constraints, flag assumptions explicitly.

Wire into `/arch-sync`, `/dev-plan`, and `/standards-check` as a "Before Step 1" directive. Also add to the existing `/adr` skill which already does this conversationally.

### IMPL-003: `/dev-implement-subtask` agent
**Domain:** agent
**Files:**
- Create: `.claude/skills/dev-implement-subtask/SKILL.md`

**Depends on:** none
**Parallel with:** IMPL-005, IMPL-006
**Estimated:** 180 lines

**Description:**
Code-writing agent that implements exactly one YAML subtask from a `/dev-plan` output. Hard constraints: only touch files in `files_to_create` and `files_to_modify`; never touch test files; never disable Kconfig safety options. Steps: load constraints → search cocoindex for patterns → implement → build check → iterate up to 5x. Includes diff cap (200 lines max modification to existing files per iteration).

Composes inline: Zephyr coding standards section, `architecture-constraints.md` constraints. Finds test patterns from existing 7 test suites via cocoindex search — no FFF needed.

### IMPL-004: `/dev-coordinate` agent
**Domain:** agent
**Files:**
- Create: `.claude/skills/dev-coordinate/SKILL.md`

**Depends on:** IMPL-003 (needs /dev-implement-subtask to exist)
**Parallel with:** none — sequential
**Estimated:** 160 lines

**Description:**
Reads an `impl-plan-*.md` file, parses subtask YAML blocks, dispatches `/dev-implement-subtask` for each subtask in dependency order. After each subtask: runs `/standards-check`. If BLOCK: feeds findings back to implement-subtask (3 iterations max then escalate). After all subtasks: runs `west twister`. Writes summary to `docs/plans/impl-summary-<slug>.md`.

Phase 1 dispatches sequentially (user invokes each subtask). Phase 3 fan-out is future work.

### IMPL-005: `/test-write` agent
**Domain:** agent
**Files:**
- Create: `.claude/skills/test-write/SKILL.md`

**Depends on:** IMPL-001 (ask_until_understood)
**Parallel with:** IMPL-003, IMPL-006
**Estimated:** 180 lines

**Description:**
Independent test writer. HARD CONSTRAINT: must NOT read any files under `src/`. Derives tests from REQ file only — never from implementation. Outputs ztest suites following the existing project pattern (native_sim, zbus listeners, DT-defined fake sensors, semaphore-based event waiting — see `tests/fake_sensors/src/main.c` and `tests/mqtt_publisher/src/main.c` for canonical examples). Lists [hil] and [manual] deferred criteria. Dry-run builds via `west twister -p native_sim`. No Renode, no Robot Framework — not set up in this project.

### IMPL-006: `/arch-validate` agent
**Domain:** agent
**Files:**
- Create: `.claude/skills/arch-validate/SKILL.md`

**Depends on:** IMPL-001 (ask_until_understood)
**Parallel with:** IMPL-005
**Estimated:** 120 lines

**Description:**
Validates a git diff against `architecture-constraints.md` and accepted ADRs — NOT against a YAML schema. Complements `/standards-check` (which checks code patterns) by checking architecture-level rules. Steps: load constraints → extract rules → analyze diff → produce compliance table. Output: `docs/arch/validation-report-<timestamp>.md` with COMPLIANT/VIOLATION/NOT APPLICABLE status per constraint.

### IMPL-007: `/sec-arch-review` agent
**Domain:** agent
**Files:**
- Create: `.claude/skills/sec-arch-review/SKILL.md`

**Depends on:** IMPL-006 (delegates to /arch-validate)
**Parallel with:** none
**Estimated:** 150 lines

**Description:**
Pre-merge security + architecture review. Delegates architecture check to `/arch-validate`. Adds: hardcoded credential scan, disabled security Kconfig flags (HW_STACK_PROTECTION, FORTIFY_SOURCE), banned crypto (MD5, SHA1, DES, RC4), TLS verification, acceptance criterion cross-reference. No CRA-specific checks — this project has no CRA obligations. Output: `docs/reviews/sec-arch-<timestamp>.md`.

### IMPL-008: Integration: wire `ask_until_understood` into existing agents
**Domain:** refactor
**Files:**
- Modify: `.claude/skills/arch-sync/SKILL.md`
- Modify: `.claude/skills/dev-plan/SKILL.md`
- Modify: `.claude/skills/standards-check/SKILL.md`

**Depends on:** IMPL-001
**Parallel with:** IMPL-003, IMPL-004, IMPL-005, IMPL-006, IMPL-007 (no file overlap)
**Estimated:** 30 lines total

**Description:**
Add "apply ask_until_understood" directive to the start of each agent's Steps section. For `/arch-sync`: before Step 1. For `/dev-plan`: before Step 0 (already has this inline, formalize). For `/standards-check`: before Step 1 (when given a feature description rather than a git ref).

---

## Dependency Graph

```
IMPL-001 (ask_until_understood) ──┬──► IMPL-005 (/test-write)
                                  ├──► IMPL-006 (/arch-validate)
                                  └──► IMPL-008 (wire into existing)

IMPL-003 (/dev-implement-subtask) ──► IMPL-004 (/dev-coordinate)

IMPL-006 (/arch-validate) ──► IMPL-007 (/sec-arch-review)
```

## File Conflict Report

No conflicts. Every agent creates exactly one file in `.claude/skills/<name>/SKILL.md`. IMPL-008 modifies files created by IMPL-001 batch — this is intentional and declared as a dependency.

## Implementation Order (by wave)

| Wave | Subtask | Rationale |
|---|---|---|
| **Wave 1** | IMPL-001 | Pure skill (no tool calls), unblocks everything else |
| **Wave 2** | IMPL-008 | Trivial wiring — completes immediately after Wave 1 |
| **Wave 3** | IMPL-003 + IMPL-005 + IMPL-006 | Three independent agents, fully parallel. IMPL-003 and IMPL-005 share no dependencies |
| **Wave 4** | IMPL-004 | Needs IMPL-003 to exist |
| **Wave 5** | IMPL-007 | Needs IMPL-006 to exist |

## Estimated Totals

| Metric | Count |
|---|---|
| Skills to create | 1 (`ask_until_understood`) |
| Agents to create | 5 (`/dev-implement-subtask`, `/dev-coordinate`, `/test-write`, `/arch-validate`, `/sec-arch-review`) |
| Files modified | 3 (wire ask_until_understood into existing 3 agents) |
| Total new lines | ~950 |
| Waves | 5 |

## Deferred / Out of Scope

These agents were in the original plan but are NOT included here:

| Agent | Why deferred |
|---|---|
| `/req-engineer` | Project has 16 accepted ADRs and 14-page architecture docs. Requirements ingestion is done. Would be valuable for a brand-new subsystem (e.g., adding LoRaWAN). |
| `/release-prep` | Requires `west spdx` (SPDX SBOM generation), `git-cliff`, `osv-scanner`, `sbomqs` — none of which are set up. This is a Phase 2+ concern if the project ships production firmware. |
| `/cra-audit` | EU Cyber Resilience Act is not a requirement for this project. The agent exists in the plan as a reusable pattern. |

## Success Criteria

- [ ] `ask_until_understood` wired into all human-facing agents
- [ ] `/dev-coordinate` can successfully read a plan from `/dev-plan` and dispatch subtasks sequentially
- [ ] `/dev-implement-subtask` respects file allowlists (verified by `/standards-check`)
- [ ] `/test-write` produces compilable tests following the zbus-listener pattern (no FFF, no src/ access)
- [ ] `/arch-validate` correctly identifies ADR constraint violations in a test diff
- [ ] `/sec-arch-review` delegates to `/arch-validate` and adds security-only checks
- [ ] All skills pass `pre-commit run --all-files`
