---
name: standards-check
description: Use after every implementation subtask, build iteration, or when asked to audit changed code
argument-hint: "[git-ref or file...]"
disable-model-invocation: false
---

# Standards Check — Fast Grep-Based Audit

**Argument received** (optional): `$0` — a commit range (e.g. `HEAD~1..HEAD`,
`master..HEAD`), specific files, or empty (defaults to uncommitted changes).

This skill runs fast, deterministic, grep-based checks against changed files.
No deep reasoning — designed for a smaller/faster model. Runs after every
implementation subtask and build iteration.

---

## Step 1 — Collect the diff

Determine what to check based on the argument:

- **No argument:** uncommitted changes (`git diff` + `git diff --staged`)
- **Commit range** (contains `..` or `~`): `git diff <range>`
- **`HEAD`**: `git diff HEAD~1..HEAD` (last commit)
- **File paths**: `git diff -- <files>`

Run both:

```bash
git diff --stat <range-or-flags>
git diff <range-or-flags>
```

If the combined diff is empty, say nothing to check and stop.

---

## Step 2 — Load constraint context

Read these files to prime the checks:

```bash
# Read the banned patterns and architecture rules
# from the project's CLAUDE.md — the "Architecture rules" and
# "Embedded C coding rules" sections.
```

Also read `docs/architecture/architecture-constraints.md` for the
ADR constraint table.

Key constraints to internalise:

- **No heap:** `malloc`, `free`, `k_malloc`, `k_free`, `k_calloc`
- **zbus channel ownership:** `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel; never in a header
- **`env_sensor_data` is flat:** no pointers, no heap fields
- **`main.c` is minimal:** `LOG_MODULE_REGISTER` + `return 0` only
- **One event = one physical measurement**
- **No sensor manager; no polling loops; no tight coupling**
- **`sensor_uid` is the identity key — never hardcoded in consumers**
- **Apps compose via Kconfig only — no `target_link_libraries()` in app `CMakeLists.txt`**
- **Fake sensors: production-quality, DT-defined, `LISTIFY` + `DT_INST`**

---

## Step 3 — Banned pattern scan (BLOCKING)

For each changed `.c`, `.h`, `CMakeLists.txt`, `Kconfig`, `.conf`, `.overlay`,
or `.yml` file, run these exact greps. Any hit is a blocker.

### 3.1 Unsafe C functions

```bash
grep -nE '\b(strcpy|sprintf|gets|atoi)\b' <changed-files>
```

**Why blocking:** These functions have no bounds checking. Use `strncpy`,
`snprintf`, `fgets`, `strtol` instead.

### 3.2 printk in non-test code

```bash
grep -rn 'printk' <changed-files-under-src-or-lib/>
```

**Allowed in:** `tests/`, `test_*.c` files. Blocking anywhere else.
Use `LOG_DBG`, `LOG_INF`, `LOG_WRN`, `LOG_ERR` instead.

### 3.3 Hardcoded crypto keys or secrets

```bash
grep -nEi '(key|secret|password|token|apikey|api_key)\s*=\s*["'"'"']?[0-9a-fA-F]{16,}' <changed-files>
grep -nEi '#define\s+\w*(KEY|SECRET|PASSWORD|TOKEN)\w*\s+[0-9a-fA-F]{16,}' <changed-files>
```

**Flag as suspicious** — ask for confirmation. If confirmed as a secret, block.
Kconfig defaults for keys are also blocking — keys must come from runtime config.

### 3.4 .tflite / TensorFlow Lite

```bash
grep -rn '\.tflite' <changed-files>
grep -rn 'CONFIG_TFLITE_MICRO' <changed-files>
```

**Blocking.** Not allowed in this project.

### 3.5 Hardcoded hex addresses in I2C/SPI

```bash
grep -nEi '(i2c|spi).*0x[0-9a-fA-F]{2}' <changed-files>
```

Not automatically blocking — flag as suspicious. If the address is from
Devicetree, it should use `_dt` APIs instead. If it's a DT node label
string, it's fine.

---

## Step 4 — Zephyr idiom compliance

For each changed `.c` and `.h` file, check:

### 4.1 Bus API variants

```bash
# Flag raw i2c_write, spi_transceive (without _dt suffix)
grep -nE '\b(i2c_write|i2c_read|i2c_burst_read|i2c_burst_write|spi_read|spi_write|spi_transceive)\s*\(' <changed-files> | grep -v '_dt'
```

**Blocking.** Use `i2c_write_dt()`, `spi_transceive_dt()`, etc. with `i2c_dt_spec` / `spi_dt_spec`.

### 4.2 LOG_MODULE_REGISTER

```bash
# For each .c file in the diff, check if it has LOG_MODULE_REGISTER
for f in <changed-.c-files>; do
  if grep -q 'printf' "$f" && ! grep -q 'LOG_MODULE_REGISTER' "$f"; then
    echo "MISSING: $f — has printf but no LOG_MODULE_REGISTER"
  fi
done
```

**Simpler check:** for each changed `.c` file under `lib/` or `apps/` that
uses any `LOG_*` macro, verify `LOG_MODULE_REGISTER` appears in the file.
Flag if missing.

### 4.3 DT_NODELABEL string anti-pattern

```bash
grep -nE 'DT_NODELABEL\(\w+\)' <changed-files> | grep -vE '#define|DT_FOREACH|DT_INST|DT_CHOSEN|LISTIFY|\.dts|\.overlay'
```

**Flag.** `DT_NODELABEL()` used as a runtime string (not inside a DT macro
expansion) suggests hardcoded hardware references. Should be from Devicetree.

### 4.4 zbus channel definition location

```bash
# ZBUS_CHAN_DEFINE in a .h file is always blocking
grep -n 'ZBUS_CHAN_DEFINE' <changed-.h-files>
```

**Blocking.** `ZBUS_CHAN_DEFINE` must only appear in `.c` files.

### 4.5 No heap

```bash
grep -nE '\b(malloc|free|k_malloc|k_free|k_calloc)\b' <changed-files>
```

**Blocking.** All allocations must be static or stack.

### 4.6 Silently discarded return values

```bash
grep -nE '^\s*(void\s*)?\b(zbus_chan_pub|k_sem_take|k_msgq_put|k_msgq_get)\s*\(' <changed-files>
grep -nE '=\s*\b(zbus_chan_pub|k_sem_take)\s*\(' <changed-files> | grep -v '^\s*(int|err)\s'
# Flag lines that call these functions but don't capture or check the return value
```

**Non-blocking suggestion.** Unchecked return values from IPC primitives
can silently drop messages or miss timeouts. Always check `ret == 0` or
the equivalent.

---

## Step 5 — KISS/DRY/SOLID (non-blocking suggestions)

These are heuristics — flag but don't block:

### 5.1 Long functions

```bash
# Functions > 50 lines (approximate — count lines between function def and next)
grep -nE '^(static\s+)?\w+\s+\w+\s*\(' <changed-files>
```

For each function definition found, roughly estimate its length in the diff.
Flag any > 50 lines.

### 5.2 Deep nesting

For each changed function, scan for `if`/`for`/`while` nesting.

```bash
# Count leading whitespace depth within function bodies
grep -nE '^\s{12,}(if|for|while|switch)\b' <changed-files>
```

**Flag.** >12 spaces of indent in control flow suggests ≥3 levels of nesting.
Consider extracting inner blocks to helper functions.

### 5.3 Duplication check

```bash
# Look for identical or near-identical logic blocks in the diff
# If the same pattern of 3+ non-trivial lines appears in 2+ files, flag it
```

Manual scan. If the diff adds similar-looking error-handling or setup code
in multiple files, suggest extracting a shared utility.

### 5.4 Single responsibility

For each changed module, read the first 30 lines (header comment, includes,
file-scope globals) and check: does this module handle one clear concern?
Flag if it mixes e.g. HTTP parsing + sensor sampling + display rendering.

---

## Step 6 — ADR constraint cross-reference

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

## Step 7 — Write the report

Create `docs/reviews/` if it doesn't exist, then write:

```
docs/reviews/standards-YYYY-MM-DD-HHMMSS.md
```

Use the current UTC timestamp.

### Report template

```markdown
# Standards Check — YYYY-MM-DD HH:MM UTC

**Files checked:**
- path/to/file1.c
- path/to/file2.h

**Diff size:** N files, M lines changed

## Blocking Issues (must fix)
<!-- Each issue: file:line — rule — explanation — suggested fix -->
<!-- If none: "No blocking issues found." -->

## Improvement Suggestions (non-blocking)
<!-- Each suggestion: file:line — principle — observation — suggestion -->
<!-- If none: "No suggestions." -->

## ADR Constraint Compliance

| Constraint | Status | Evidence |
|---|---|---|
| ADR-002 zbus ownership | COMPLIANT | ZBUS_CHAN_DEFINE only in .c files |
| ADR-003 flat struct | N/A | No env_sensor_data changes |
| ADR-004 trigger-driven | COMPLIANT | No polling loops or sensor managers |
| ... | ... | ... |

## Verdict

**PASS** | **PASS WITH SUGGESTIONS** | **BLOCK (N blockers)**

<!-- A terse final line -->
```

Keep the report concise:
- **PASS:** under 200 words
- **BLOCK:** under 500 words
- One-sentence per issue; no essays

---

---

## Red Flags — STOP and re-check

- Claiming PASS without running ALL grep checks
- Trusting a previous run's output instead of running fresh
- Skipping Step 2 (constraint loading) because "I remember them"
- Skipping Step 4 (Zephyr idioms) because "only small changes"
- Marking ADR constraints as N/A without checking the diff
- Using "looks fine" / "seems compliant" / "should be okay"
- **Any wording implying compliance without having RUN the verification**

## Rules

- **READ-ONLY:** Never modify code during a standards check.
- **Fast path:** If the diff only touches `docs/` or `tests/`, relax checks:
  - `printk` is allowed in test files
  - ADR constraints mostly don't apply to docs
  - Only run the banned pattern scan (Step 3) and skip Steps 4-6
- **Be mechanical, not judgmental.** This skill runs greps and reports what
  they find. It does not reason about intent or design quality — that's what
  `/review` is for.
- **Never block on style alone.** Banned patterns and architecture violations
  are blockers. Deep nesting and long functions are suggestions.
- **Silence is success.** If all greps come back empty and ADR constraints
  are compliant, the report is a quick PASS.
