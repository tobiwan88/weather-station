---
name: standards-check
description: Use after every implementation subtask, build iteration, or when asked to audit changed code. Runs pre-commit gate, then reviews against project-specific constraints from ADRs and CLAUDE.md.
argument-hint: "[git-ref or file...]"
disable-model-invocation: false
---

# Standards Check — Pre-Commit Gate + Constraint Review

**Argument received** (optional): `$0` — a commit range (e.g. `HEAD~1..HEAD`,
`master..HEAD`), specific files, or empty (defaults to uncommitted changes).

This skill runs a fast pre-commit gate (replacing grep-based checks) followed by
a project-constraint review. Use after every implementation subtask and build
iteration.

---

## Step 1 — Collect the diff

Determine what to check based on the argument:

- **No argument:** uncommitted changes (`git diff` + `git diff --staged`)
- **Commit range** (contains `..` or `~`): `git diff --name-only <range>`
- **`HEAD`**: `git diff --name-only HEAD~1..HEAD`
- **File paths**: use as-is

Run:

```bash
git diff --stat <range-or-flags>
git diff --name-only <range-or-flags>
```

If the diff is empty, say nothing to check and stop.

---

## Step 2 — Pre-commit gate (fast, covers SCA-like checks)

Run pre-commit on the changed files:

```bash
pre-commit run --files <list-of-changed-files>
```

This catches: trailing whitespace, end-of-file fixer, merge conflicts, mixed line
endings, large file additions, clang-format issues, cmake-format issues,
yamllint, markdownlint. Any failure here is a blocker — the build-and-test gate
already requires this pass before commit.

If pre-commit fails, report the failures and stop. The change is not ready for review.

---

## Step 3 — Load project constraints

Read these files to understand the enforcement rules:

1. `CLAUDE.md` — the "Architecture rules" and "Embedded C coding rules" sections
2. `docs/architecture/architecture-constraints.md` — the ADR constraint table

These are the **actual** project constraints. Do not invent hypothetical rules.
Every check below derives from a named ADR or a rule in CLAUDE.md.

---

## Step 4 — Project-specific constraint scans

For each changed `.c`, `.h`, `CMakeLists.txt`, `Kconfig`, `.conf`, `.overlay`,
or `.yml` file, run these checks. Each maps to a named project constraint.

### 4.1 No heap allocations (CLAUDE.md / ADR-002, ADR-003)

```bash
grep -nE '\b(malloc|free|k_malloc|k_free|k_calloc)\b' <changed-files>
```

**Blocking.** All allocations must be static or stack-allocated.

### 4.2 zbus channel definition location (ADR-002)

```bash
grep -n 'ZBUS_CHAN_DEFINE' <changed-.h-files>
```

**Blocking.** `ZBUS_CHAN_DEFINE` must only appear in `.c` files.

### 4.3 printk in non-test code (CLAUDE.md)

```bash
grep -rn 'printk' <changed-files-under-src-or-lib/>
```

**Allowed in:** `tests/`, `test_*.c` files. Blocking anywhere else.
Use `LOG_DBG`, `LOG_INF`, `LOG_WRN`, `LOG_ERR` instead.

### 4.4 Bus API variants — no raw i2c/spi without _dt suffix

```bash
grep -nE '\b(i2c_write|i2c_read|i2c_burst_read|i2c_burst_write|spi_read|spi_write|spi_transceive)\s*\(' <changed-files> | grep -v '_dt'
```

**Blocking.** Use `i2c_write_dt()`, `spi_transceive_dt()`, etc. with `i2c_dt_spec`.

### 4.5 Unchecked zbus return values (ADR-002)

```bash
grep -nE '^\s*zbus_chan_pub\s*\(' <changed-files>
grep -nE '^\s*k_msgq_put\s*\(' <changed-files>
```

**Warning.** Unchecked return values from IPC primitives can silently drop messages.
Always check `(ret == 0)`.

### 4.6 Kconfig-only composition (ADR-008)

```bash
grep -n 'target_link_libraries' <changed-CMakeLists.txt-files-under-apps/>
```

**Blocking.** App `CMakeLists.txt` must never contain feature-selection logic.

---

## Step 5 — ADR constraint cross-reference

Cross-check the diff against the constraints from
`docs/architecture/architecture-constraints.md`. For each constraint that
applies to a changed file, answer: does the change comply?

**Mechanical checklist:**

| Constraint (ADR) | Check |
|---|---|
| ADR-002 zbus ownership | Is `ZBUS_CHAN_DEFINE` only in `.c` files? |
| ADR-003 flat struct | Does any new `env_sensor_data` usage add pointers/heap? |
| ADR-004 trigger-driven | Is there any new polling loop or sensor manager? |
| ADR-005 fake sensor UIDs | Are fake sensor UIDs in range 0x0001–0x00FF? |
| ADR-007 display decoupling | Does `display_manager` include anything from `connectivity`? |
| ADR-008 Kconfig composition | Any `target_link_libraries()` in app `CMakeLists.txt`? |
| ADR-009 native_sim first | Is `k_busy_wait()` guarded with `#if !defined(CONFIG_NATIVE_SIM)`? |
| ADR-011 HTTP dashboard | Are spinlock callbacks non-blocking? |
| ADR-013 MQTT passwords | Are passwords base64 in settings, not plaintext in code? |
| ADR-014 FOTA hardware-only | Is `CONFIG_FOTA_CONFIRM` gated on `BOOTLOADER_MCUBOOT`? |

Mark each: **COMPLIANT**, **N/A** (constraint doesn't touch changed files), or **VIOLATION**.

---

## Step 6 — Write the report

Derive a timestamp:

```bash
date -u +%Y-%m-%dT%H%M%S
```

Create `docs/reviews/` if it doesn't exist, then write:

```
docs/reviews/standards-<timestamp>.md
```

### Report template

```markdown
# Standards Check — YYYY-MM-DD HH:MM UTC

**Files checked:**
- path/to/file1.c
- path/to/file2.h

**Diff size:** N files, M lines changed

## Pre-commit gate
**PASS** | **FAIL (N failures)** — <list failures>

## Blocking Issues (must fix)
<!-- Each issue: file:line — rule — explanation — suggested fix -->
<!-- If none: "No blocking issues found." -->

## Warnings (should fix)
<!-- Each: file:line — rule — observation — suggestion -->

## ADR Constraint Compliance

| Constraint | Status | Evidence |
|---|---|---|
| ADR-002 zbus ownership | COMPLIANT | ZBUS_CHAN_DEFINE only in .c files |
| ADR-003 flat struct | N/A | No env_sensor_data changes |
| ADR-004 trigger-driven | COMPLIANT | No polling loops or sensor managers |
| ... | ... | ... |

## Verdict

**PASS** | **PASS WITH WARNINGS** | **BLOCK**
```

Keep reports concise:
- **PASS:** under 150 words
- **BLOCK:** under 400 words
- One-sentence per issue

---

## Red Flags — STOP and re-check

- Claiming PASS without running pre-commit
- Claiming PASS without checking ALL ADR constraints in Step 5
- Marking ADR constraints as N/A without checking the diff
- Using "looks fine" / "seems compliant" / "should be okay"
- Skipping Step 3 (constraint loading) because "I remember them"
- **Any wording implying compliance without having RUN the verification**

## Rules

- **READ-ONLY:** Never modify code during a standards check.
- **Fast path for docs:** If the diff only touches `docs/`, run pre-commit and skip
  Steps 4-5 — only ADR-005 (UIDs in docs) might apply.
- **Silence is success.** If all constraint scans pass, the report is a quick PASS.
- **Be mechanical, not judgmental.** This skill checks compliance against named rules.
  It does not reason about intent or design quality — that's what `/review` is for.
