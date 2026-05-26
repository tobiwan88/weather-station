---
name: review
description: Use when completing tasks, implementing major features, or before merging. Spawns parallel review agents for architecture, security, C quality, embedded, and test coverage. Invoke when the user says "review code", "code review", "check my work", or before creating a PR.
argument-hint: "[commit-range or file...]"
allowed-tools: Read, Bash, Task, Grep
---

# Code Review with Specialized Agents

**Argument received** (optional): `$0` — a commit range (e.g. `HEAD~3..HEAD`,
`master..HEAD`), specific files, or empty (defaults to uncommitted changes).

This skill collects the patch, then spawns **parallel sub-agents** that each
review from a different angle. Results are synthesised into a single report.

## Step 1 — Collect the patch context

Use the review context collection script instead of manual git commands:

```bash
scripts/collect-review-context.sh [git-ref] [file...]
```

**Examples:**
```bash
scripts/collect-review-context.sh                  # uncommitted changes
scripts/collect-review-context.sh HEAD~1..HEAD     # last commit
scripts/collect-review-context.sh master..HEAD     # commits since master
```

**Output:** JSON to stdout with patch content, changed files list, commit messages, pre-commit results, and file headers (first 30 lines of each changed file).

**JSON output schema:**
```json
{
  "script": "collect-review-context",
  "timestamp": "2026-05-25T12:00:00Z",
  "verdict": "READY|EMPTY",
  "files_changed": ["lib/foo/src/foo.c", "lib/foo/include/foo/foo.h"],
  "file_count": 2,
  "patch_lines": 150,
  "diff_stat": "...",
  "patch_content": "...",
  "commit_messages": "...",
  "pre_commit_output": "...",
  "file_headers": [
    {"path": "lib/foo/src/foo.c", "header": "...first 30 lines..."},
    {"path": "lib/foo/include/foo/foo.h", "header": "...first 30 lines..."}
  ],
  "summary": "Collected context for 2 files, 150 lines of diff."
}
```

**Exit codes:** 0 = context collected, 1 = no changes to review

If the patch is empty (verdict: "EMPTY"), tell the user there is nothing to review and stop.

## Step 2 — Spawn review agents in parallel

Launch **all agents in a single message** so they run in parallel.
Use the fastest available model for each agent to keep cost low and speed high.

Agent prompt templates are in `references/`. Load the appropriate template and insert the patch, file context, and architecture rules as placeholders.

### Agent 1: Architecture Review

See [`references/arch-review-prompt`](references/arch-review-prompt) for the full prompt template. Insert `{architecture_rules}`, `{patch_content}`, and `{file_headers}`.

### Agent 2: Security Review

See [`references/security-review-prompt`](references/security-review-prompt) for the full prompt template. Insert `{patch_content}`.

### Agent 3: Coding Standards & C Quality

See [`references/c-quality-review-prompt`](references/c-quality-review-prompt) for the full prompt template. Insert `{patch_content}` and `{pre_commit_output}`.

### Agent 4: Embedded & Resource Review

See [`references/embedded-review-prompt`](references/embedded-review-prompt) for the full prompt template. Insert `{patch_content}`.

### Agent 5: Test Coverage Review

See [`references/test-coverage-review-prompt`](references/test-coverage-review-prompt) for the full prompt template. Insert `{patch_content}`.

## Step 3 — Synthesise the report

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

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "I can review in my head" | Reviewing your own code misses blind spots. Use agents. |
| "The patch is small, skip some agents" | All 5 agents run. Small patches are fast. |
| "Pre-commit already caught everything" | Pre-commit catches style, not arch/security/embedded issues. |
| "I'll skip the test coverage agent" | Missing test gaps is how regressions ship. |

## Rules

- Always spawn all 5 agents in a **single message** (parallel execution)
- Use the fastest available model for each review agent
- Never modify code during review — this skill is read-only
- If the patch is large (>500 lines), mention that the review may miss
  issues and suggest splitting the change
- Keep individual agent responses under 300 words to avoid noise
- The prompt templates are in `references/`. Load them, insert context, and dispatch.
- Do not hardcode model names in the skill — use the fastest available model for your platform.

**Terminal state:** Synthesised review report presented to the user with a clear verdict (PASS / PASS WITH WARNINGS / NEEDS CHANGES).
