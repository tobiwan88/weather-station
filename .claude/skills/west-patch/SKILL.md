---
name: west-patch
description: Use when adding an out-of-tree patch to Zephyr or another west module. Manages patches in zephyr/patches/ tracked by patches.yml. Invoke when the user says "add patch", "patch Zephyr", "west patch", or needs to modify upstream module source.
argument-hint: "<short description of the fix>"
allowed-tools: Bash, Read, Edit, Write, Glob, Grep
---

# Add a Zephyr Patch via west patch

**Argument received** (optional): `$0`

Patches to Zephyr (or other west modules) live in `zephyr/patches/` and are
tracked in `zephyr/patches.yml`.  They are applied with `west patch apply` after
`west update`.

## Layout

```
weather-station/
└── zephyr/
    ├── patches.yml
    └── patches/
        └── zephyr/          # one subdirectory per patched module
            └── 0001-*.patch
```

## Step-by-step

### 1. Make the fix in the module repo

```bash
cd /home/zephyr/workspace/zephyr
git checkout -b fix/<short-name>
# edit the file(s)
git add <file>
git commit -m "subsystem: short description

Longer explanation of the bug and fix.

Signed-off-by: Tobias Meyer <tobiuhg@gmail.com>"
```

### 2. Export the patch

```bash
git format-patch HEAD~1 --stdout \
  > /home/zephyr/workspace/weather-station/zephyr/patches/zephyr/\
<NNN>-<short-name>.patch
```

Use the next available sequence number (`NNN`) for ordering.

### 3. Compute the sha256sum

```bash
sha256sum /home/zephyr/workspace/weather-station/zephyr/patches/zephyr/<patch-file>
```

### 4. Add an entry to `zephyr/patches.yml`

```yaml
patches:
  - path: zephyr/<patch-file>       # relative to zephyr/patches/
    sha256sum: <hash-from-step-3>
    module: zephyr
    author: Tobias Meyer
    email: tobiuhg@gmail.com
    date: YYYY-MM-DD
    upstreamable: true|false
    apply-command: git am
    comments: |
      One-paragraph description: what bug this fixes, why the fix is correct,
      and the upstream PR URL if one exists.
```

### 5. Reset the module to its manifest revision

```bash
cd /home/zephyr/workspace/zephyr
git reset --hard v4.4.0
```

### 6. Verify the patch applies cleanly

```bash
cd /home/zephyr/workspace
west patch apply
```

Expected output: `N patches applied successfully \o/`

**If the patch fails to apply:**
1. Re-checkout the fix branch in the module repo: `git checkout fix/<short-name>`
2. Rebase onto the current manifest revision if needed
3. Re-export with `git format-patch HEAD~1 --stdout`
4. Update the sha256sum in `patches.yml`
5. Reset the module again and re-verify with `west patch apply`

Do not skip verification. A patch that doesn't apply cleanly will break `west update` for everyone.

---

## Lifecycle commands

| Command | When |
|---|---|
| `west patch apply` | After `west update` or whenever patches must be active |
| `west patch clean` | Before `west update` to revert patches to the manifest baseline |
| `west patch list` | Show patch status |

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "I'll just edit Zephyr source directly" | Changes are lost on `west patch clean` / `west update`. Always use a patch. |
| "The sha256sum check can wait" | Apply fails will break `west update` for everyone. Compute it now. |
| "My patch conflicts with an existing one" | Check ordering in `patches.yml`. Patches apply sequentially. May need rebase. |
| "I'll skip `west patch apply` verification" | A broken patch blocks the entire team. Always verify. |

## Rules

- **Never edit Zephyr source directly** without a patch entry — changes are lost on `west patch clean` / `west update`.
- Keep one logical fix per patch file.
- `upstreamable: true` if the fix belongs in Zephyr mainline; add a `merge-pr:` URL once a PR is open.
- Update `sha256sum` in `patches.yml` whenever the patch file content changes.
- After adding a patch, invoke `/build-and-test` to verify the build still passes.

**Terminal state:** Patch file added to `zephyr/patches/zephyr/`, entry added to `zephyr/patches.yml`, `west patch apply` succeeds, build passes.
