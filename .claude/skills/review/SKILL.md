---
name: review
description: Review staged or committed code changes by spawning specialized sub-agents (architecture, security, coding standards, embedded, tests). Each agent reviews from a different angle.
argument-hint: "[commit-range or file...]"
---

# Code Review with Specialized Agents

**Argument received** (optional): `$0` — a commit range (e.g. `HEAD~3..HEAD`,
`master..HEAD`), specific files, or empty (defaults to uncommitted changes).

This skill collects the patch, then spawns **parallel sub-agents** that each
review from a different angle. Results are synthesised into a single report.

---

## Step 1 — Collect the patch

Determine what to review based on the argument:

- **No argument:** review uncommitted changes (`git diff` + `git diff --staged`)
- **Commit range** (contains `..` or `~`): `git diff <range>`
- **`HEAD`**: `git diff HEAD~1..HEAD` (last commit)
- **File paths**: `git diff -- <files>`

Run the appropriate command and store the full patch output. Also collect:

```bash
# Changed file list
git diff --stat <range-or-flags>

# Commit messages (if reviewing commits)
git log --oneline <range>
```

If the patch is empty, tell the user there is nothing to review and stop.

---

## Step 2 — Gather minimal context

For each changed file, read the **first 30 lines** (imports, module header,
struct definitions) to give reviewers file-level context. Do NOT read entire
files — the agents only need the patch + enough framing to understand it.

Also read the project's architecture rules from CLAUDE.md (the "Architecture
rules" and "Integration tests" sections) to include in agent prompts.

---

## Step 3 — Spawn review agents in parallel

Launch **all agents in a single message** so they run in parallel.
Use `model: "haiku"` for each agent to keep cost low and speed high.

### Agent 1: Architecture Review

```
Agent({
  description: "Architecture review",
  model: "haiku",
  prompt: <see prompt template below>
})
```

**Prompt template for Architecture agent:**

> You are reviewing a patch for a Zephyr RTOS weather-station project.
> Review ONLY for architectural concerns. Do NOT comment on style, naming,
> or security — other reviewers handle those.
>
> **Project architecture rules:**
> <paste architecture rules from CLAUDE.md>
>
> **Focus areas:**
> - zbus channel ownership: ZBUS_CHAN_DEFINE in exactly one .c per channel
> - SYS_INIT ordering: check priority conflicts with existing modules
> - Kconfig-only app composition: no target_link_libraries() in apps
> - No sensor_manager, no polling loops, no tight coupling
> - main.c must stay minimal (LOG_MODULE_REGISTER + k_sleep only)
> - env_sensor_data is a flat 20-byte struct — no pointers, no heap
> - sensor_uid is the identity key — never hardcoded in consumers
> - HTTP dashboard decoupling: POST publishes config_cmd_event, never calls subsystems directly
>
> **The patch:**
> ```diff
> <patch content>
> ```
>
> **File context (headers/imports of changed files):**
> <file headers>
>
> Report issues as a numbered list. For each: severity (CRITICAL / WARNING /
> NOTE), file:line, what's wrong, and what to do instead. If no issues found,
> say "No architectural issues found." Keep it under 300 words.

### Agent 2: Security Review

```
Agent({
  description: "Security review",
  model: "haiku",
  prompt: <see prompt template below>
})
```

**Prompt template for Security agent:**

> You are reviewing a patch for a Zephyr RTOS embedded project with HTTP
> and MQTT networking. Review ONLY for security concerns. Do NOT comment on
> architecture, style, or tests — other reviewers handle those.
>
> **Focus areas:**
> - Buffer overflows: fixed-size buffers, memcpy without bounds checks, stack buffers
> - Format string vulnerabilities in LOG_* or shell_print/shell_error
> - HTTP request handling: input validation, path traversal, response injection
> - MQTT: credential exposure in logs, topic injection
> - Integer overflow/underflow in sensor value conversions (Q31 arithmetic)
> - Race conditions: k_spinlock usage, zbus publish from ISR context
> - Stack overflow risk: large local variables, recursive calls
> - Secrets in code: hardcoded passwords, API keys, credentials
>
> **The patch:**
> ```diff
> <patch content>
> ```
>
> Report issues as a numbered list. For each: severity (CRITICAL / WARNING /
> NOTE), file:line, the vulnerability, and the fix. If no issues found,
> say "No security issues found." Keep it under 300 words.

### Agent 3: Coding Standards & C Quality

```
Agent({
  description: "C coding standards review",
  model: "haiku",
  prompt: <see prompt template below>
})
```

**Prompt template for Coding Standards agent:**

> You are reviewing a patch for a Zephyr RTOS project written in C99.
> Review ONLY for coding quality and C language concerns. Do NOT comment on
> architecture, security, or tests — other reviewers handle those.
>
> **Focus areas:**
> - Zephyr API usage: correct use of k_spinlock, k_sem, zbus, LOG_* macros
> - Error handling: unchecked return values from zbus_chan_pub, k_sem_take, etc.
> - Memory: stack-allocated structs that should be static, missing const qualifiers
> - Naming: Zephyr conventions (snake_case, CONFIG_ prefix for Kconfig)
> - Include hygiene: unnecessary includes, missing includes, include order
> - Macro safety: missing parentheses, multiple evaluation of arguments
> - Dead code, unreachable branches, redundant checks
> - Logging: appropriate log levels (ERR vs WRN vs INF vs DBG)
>
> **The patch:**
> ```diff
> <patch content>
> ```
>
> Report issues as a numbered list. For each: severity (WARNING / NOTE),
> file:line, the issue, and the fix. If no issues found,
> say "No coding issues found." Keep it under 300 words.

### Agent 4: Embedded & Resource Review

```
Agent({
  description: "Embedded resource review",
  model: "haiku",
  prompt: <see prompt template below>
})
```

**Prompt template for Embedded agent:**

> You are reviewing a patch for a Zephyr RTOS embedded project targeting
> both native_sim and constrained MCU hardware. Review ONLY for embedded
> systems and resource concerns. Do NOT comment on architecture, security,
> style, or tests — other reviewers handle those.
>
> **Focus areas:**
> - Stack usage: thread stack sizes, large local arrays, deep call chains
> - Heap usage: any k_malloc, k_calloc — should be avoided where possible
> - ISR safety: blocking calls in interrupt/callback context (zbus listeners
>   may run from ISR — no k_sleep, no mutex, no LOG in ISR)
> - Timing: k_sleep in critical paths, busy-wait loops, missed deadlines
> - Power: unnecessary wake-ups, polling instead of event-driven
> - Thread priorities: priority inversion risks, work queue starvation
> - Memory alignment: Q31 struct packing, padding on 32-bit vs 64-bit targets
> - Peripheral resource conflicts: UART, SPI bus sharing
>
> **The patch:**
> ```diff
> <patch content>
> ```
>
> Report issues as a numbered list. For each: severity (CRITICAL / WARNING /
> NOTE), file:line, the concern, and the recommendation. If no issues found,
> say "No embedded resource issues found." Keep it under 300 words.

### Agent 5: Test Coverage Review

```
Agent({
  description: "Test coverage review",
  model: "haiku",
  prompt: <see prompt template below>
})
```

**Prompt template for Test Coverage agent:**

> You are reviewing a patch for a Zephyr RTOS project that has both C-based
> ztest unit tests (tests/*) and Python pytest integration tests
> (tests/integration/pytest/). Review ONLY for test adequacy. Do NOT
> comment on architecture, security, or style — other reviewers handle those.
>
> **Test infrastructure:**
> - Unit tests: ztest framework under tests/<module>/ with testcase.yaml
> - Integration tests: pytest + twister_harness under tests/integration/pytest/
> - Harnesses: ShellHarness, HttpHarness, MqttHarness (Page Object Model)
> - Markers: smoke, shell, http, mqtt, e2e
>
> **Focus areas:**
> - Does the patch add/modify functionality without adding/updating tests?
> - Are edge cases covered (zero values, max values, error paths)?
> - If a new shell command was added, is it tested in test_shell.py?
> - If an HTTP endpoint changed, is test_http_api.py updated?
> - If sensor data flow changed, does test_sensor_flow.py cover it?
> - Are ztest assertions checking the right conditions?
> - For integration tests: are harness methods used (not raw shell strings)?
>
> **The patch:**
> ```diff
> <patch content>
> ```
>
> Report: what's tested, what's missing, and specific test suggestions.
> For each gap: file where the test should go and a one-line description.
> If coverage is adequate, say "Test coverage is adequate." Under 300 words.

---

## Step 4 — Synthesise the report

After all agents complete, combine their findings into a single structured
report. Group by severity:

```markdown
## Code Review Summary

### CRITICAL (must fix before merge)
- <issue from any agent>

### WARNING (should fix)
- <issue from any agent>

### NOTE (consider)
- <issue from any agent>

### Test gaps
- <from test coverage agent>

### Verdict
<PASS / PASS WITH WARNINGS / NEEDS CHANGES>
```

Deduplicate: if two agents flag the same line, merge into one finding.
Attribute each finding to its source angle (arch / security / C quality /
embedded / tests).

---

## Rules

- Always spawn all 5 agents in a **single message** (parallel execution)
- Use `model: "haiku"` for all review agents
- Never modify code during review — this skill is read-only
- If the patch is large (>500 lines), mention that the review may miss
  issues and suggest splitting the change
- Keep individual agent responses under 300 words to avoid noise
