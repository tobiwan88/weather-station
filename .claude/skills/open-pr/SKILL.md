---
name: open-pr
description: Push the current branch, open a GitHub PR, wait for CI checks to complete, and fix any issues found.
argument-hint: "[base-branch]"
disable-model-invocation: true
allowed-tools: Bash
---

# Open PR and Fix CI

Push the current branch, open a PR, then monitor CI checks and fix failures
until the PR is green.

**Argument received** (optional): base-branch=`$0` (default: `master`)

---

## Step 1 — Pre-flight checks

Before pushing, verify the local gate is green:

```bash
# Build
west build -p always -b native_sim/native/64 apps/gateway

# Smoke test
printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1

# Full test suite (ztest + pytest integration)
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N

# Lint
pre-commit run --all-files
```

Do not push if any step fails — fix first.

---

## Step 2 — Push and open the PR

```bash
git push -u origin HEAD
```

Then open the PR with `gh`:

```bash
gh pr create --base "${base_branch:-master}" \
  --title "<type>(<scope>): <imperative summary>" \
  --body "$(cat <<'EOF'
## Summary
<1–3 bullet points describing the change>

## Test plan
- [ ] `/build-and-test` passes locally
- [ ] Integration tests pass: `/run-integration-tests`
- [ ] <feature-specific manual check if applicable>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

**Title rules:**
- Conventional Commits style: `feat(scope):`, `fix(scope):`, `test(scope):`
- Under 70 characters
- Imperative mood

Print the PR URL after creation.

---

## Step 3 — Wait for CI checks

Poll the PR check status. Use `gh` to monitor:

```bash
gh pr checks <pr-number> --watch
```

If `--watch` is not available, poll manually:

```bash
gh pr checks <pr-number>
```

Wait until all checks have completed (passed or failed). Do not poll more
frequently than every 60 seconds.

---

## Step 4 — Fix failures

If any CI check fails:

1. **Read the failure log:**
   ```bash
   gh pr checks <pr-number>
   gh run view <run-id> --log-failed
   ```

2. **Diagnose the root cause.** Common failures:
   - **Build failure** → Kconfig or source error; fix and rebuild locally
   - **ztest failure** → read the `FAIL` assertion, reproduce locally with
     `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister -p native_sim/native/64 -T tests/<failing_test> --inline-logs -v -N`
   - **pytest integration failure** → reproduce with
     `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N`
   - **Lint / format failure** → `pre-commit run --all-files` and commit fixes
   - **markdownlint** → check `.md` files for trailing spaces, line length, heading style

3. **Fix locally, re-run the full gate:**
   ```bash
   west build -p always -b native_sim/native/64 apps/gateway
   ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
     west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
   pre-commit run --all-files
   ```

4. **Commit the fix (new commit, do not amend):**
   ```bash
   git add <fixed-files>
   git commit -m "fix(<scope>): <what was fixed>"
   ```

5. **Push and re-check:**
   ```bash
   git push
   ```

6. **Repeat steps 3–5** until all checks pass.

---

## Step 5 — Report result

When all checks are green, print:

> PR #<number> is green and ready for review: <url>

If checks cannot be fixed after a reasonable attempt, report the blocker
to the user with the specific failure output.

---

## Rules

- Never force-push (`--force`) unless explicitly asked
- Never skip hooks (`--no-verify`)
- Never amend published commits — always create new fix commits
- Never merge the PR — only the user or a reviewer merges
- Always run the full local gate before pushing (build → smoke → twister → pre-commit)
