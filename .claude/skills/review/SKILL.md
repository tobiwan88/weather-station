---
name: review
description: Reviews a diff, commit range or files before merging or opening a PR, using parallel reviewer subagents for correctness, security and test gaps. Use for "review code", "code review", "check my work".
argument-hint: "[commit-range or file...]"
allowed-tools: Read, Bash, Agent, Grep
---

# Code Review

**Argument** (optional): a commit range (`master...HEAD`), file paths, or empty for uncommitted changes.

This skill is read-only. Never modify code during a review.

## Step 1: Resolve the scope

- Empty: `git diff HEAD`
- Range (contains `..`): `git diff <range>`
- Files: `git diff HEAD -- <files>`

Run the chosen command with `--stat`. If it prints nothing, tell the user there is nothing to review and stop. If the diff is over 500 changed lines, warn that the review may miss issues and suggest splitting the change.

## Step 2: Spawn three reviewers in parallel

Send all three `Agent` calls in one message, each with `subagent_type: "reviewer"`. For each, read the focus prompt below, replace `{scope}` with the diff command from Step 1, and pass the result as the agent's prompt.

| Focus | Prompt file |
|---|---|
| Correctness and concurrency | `references/correctness-concurrency-prompt` |
| Security | `references/security-prompt` |
| Test coverage | `references/tests-prompt` |

## Step 3: Merge the findings

1. Collect the three reports.
2. Deduplicate: findings on the same `path:line` become one entry, tagged with every angle that reported it.
3. Drop any finding with no concrete failure scenario.
4. Rank by severity: CRITICAL, then WARNING, then NOTE. Within a severity, sort by path.

Present the result:

```markdown
## Code Review

### CRITICAL (fix before merge)
- `path:line`: problem, fix (angle: correctness, security)

### WARNING (should fix)
- ...

### NOTE (consider)
- ...

### Test gaps
- `test file`: gap

### Verdict
PASS | PASS WITH WARNINGS | NEEDS CHANGES
```

NEEDS CHANGES if there is any CRITICAL finding, PASS WITH WARNINGS if there is any WARNING, otherwise PASS.
