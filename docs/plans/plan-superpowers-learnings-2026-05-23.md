# Plan — Superpowers Learnings Applied
> 5 steps to CSO-optimize skills, add verification gates, and add Zephyr-adapted debugging
> 2026-05-23

## Step 1: CSO-optimize all skill descriptions
**Effort:** 5 min | **Files:** 9 SKILL.md files
Change descriptions from "what it does" to "when to use." Follow superpowers CSO rule: descriptions that summarize workflow create shortcuts Claude takes instead of reading skill body.

## Step 2: Add verification gate to /build-and-test
**Effort:** 10 min | **Files:** .claude/skills/build-and-test/SKILL.md
Add IDENTIFY→RUN→READ→VERIFY gate function. Current skill runs commands but allows "should pass" shortcuts. After this, the agent must produce fresh evidence for each gate step.

## Step 3: Add HARD-GATE markers to /dev-implement-subtask spec
**Effort:** 5 min | **Files:** docs/plans/impl-plan-missing-agents-2026-05-23.md
Update the /dev-implement-subtask description in the plan to require HARD-GATE markers around file allowlist and build-check constraints.

## Step 4: Create /systematic-debug skill for Zephyr context
**Effort:** 30 min | **Files:** .claude/skills/systematic-debug/SKILL.md
New skill. Adapted from superpowers' 4-phase process, specialized for Zephyr build failures, twister failures, NSOS EPOLL errors, and common pitfalls documented in CLAUDE.md.

## Step 5: Add red flags tables to enforcement agents
**Effort:** 10 min | **Files:** .claude/skills/standards-check/SKILL.md, .claude/skills/build-and-test/SKILL.md
Add "Red Flags — STOP" section with self-check patterns. Agent catches itself rationalizing shortcuts.
