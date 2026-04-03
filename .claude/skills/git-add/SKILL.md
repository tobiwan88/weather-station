---
name: git-add
description: Stage explicitly listed files and create a conventional commit. Run only after build, smoke-test, Twister, and pre-commit are all green.
disable-model-invocation: true
allowed-tools: Bash(git *)
---

# Git Add & Commit

Stage the specified files and create a commit for the current logical unit of work.

## Steps

### 1. Verify the gate passed
Only run this after build, smoke-test, Twister, and pre-commit are all green.
Use `/build-and-test` if in doubt.

### 2. Stage files
```bash
git add <explicitly listed files>
```
Never use `git add -A` or `git add .` — avoid accidentally staging `.env`, build artefacts, or unrelated files.

### 3. Commit
```bash
git commit -m "<type>(<scope>): <imperative summary>"
```

**type:** `feat` | `fix` | `refactor` | `test` | `docs` | `chore`
**scope:** library or app name (e.g. `fake_sensors`, `gateway`, `http_dashboard`)
**summary:** imperative mood, ≤72 chars, no trailing period

Optional body (add when change is non-obvious):
```
feat(fake_sensor): publish humidity as separate zbus event

Split env_sensor_publish() into two calls — one per sensor_type.
Updated tests/fake_sensor/src/main.c expectations accordingly.
No Kconfig or DTS changes.
```
One sentence per key decision: file paths, symbol names, config keys changed.

### 4. Repeat for each logical unit
Run steps 1–3 for every independent change before moving on.

## Hand off
When all commits on the branch are done, tell the user:
> "Branch `<name>` is ready. All builds pass and tests are green.
> Run `git merge --no-ff <name>` or open a PR to merge into main."

Do **not** merge, push, or open a PR unless explicitly asked.

## Rules
- Never commit directly to `main` or `master`
- Never skip hooks (`--no-verify`)
- Never amend a published commit
