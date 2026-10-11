---
name: west-patch
description: Use when adding, updating or removing an out-of-tree patch to Zephyr or another west module. Patches live in zephyr/patches/ and are tracked in zephyr/patches.yml. Triggers: "add patch", "patch Zephyr", "west patch".
argument-hint: "<short description of the fix>"
allowed-tools: Bash, Read, Edit, Write, Glob, Grep
---

# Add or Update a West Patch

**Argument** (optional): `$0`

Patches are `git format-patch` files in `zephyr/patches/<module>/`, listed in
`zephyr/patches.yml`. `west patch apply` applies them in list order and refuses any file
whose sha256sum differs from its entry. Run every command through `scripts/ws.sh`, which
runs from the west workspace root.

No patches are applied today (`patches: []`); create `zephyr/patches/zephyr/` when adding the first one.

## Steps

1. Commit the fix in the module repo:
   `scripts/ws.sh 'git -C zephyr checkout -b fix/<name> && git -C zephyr add <file> && git -C zephyr commit -s -m "<subsystem>: <summary>"'`
2. Export it as the next patch number:
   `scripts/ws.sh 'git -C zephyr format-patch -1 HEAD --stdout > zephyr/patches/zephyr/<NNNN>-<name>.patch'`
3. Add an entry to `zephyr/patches.yml`. `path` is relative to `zephyr/patches/`:
   ```yaml
     - path: zephyr/<NNNN>-<name>.patch
       sha256sum: <from step 4>
       module: zephyr
       author: Tobias Meyer
       email: tobiuhg@gmail.com
       date: YYYY-MM-DD
       upstreamable: true|false
       apply-command: git am
       comments: |
         What bug this fixes and why the fix is correct.
   ```
   Add `merge-pr: <url>` once an upstream PR is open.
4. Compute the checksum: `scripts/ws.sh 'sha256sum zephyr/patches/zephyr/<NNNN>-<name>.patch'`.
   Recompute whenever the patch file changes.
5. Verify: `scripts/ws.sh 'west patch clean && west patch apply'`, expecting
   `N patches applied successfully`. `clean` discards local changes in the module repo.
   Then run `/build-and-test`.

## Other commands

| Inside `scripts/ws.sh '...'` | Use |
|---|---|
| `west patch list` | Show patch status |
| `west patch apply -r` | Roll back the applied set if one patch fails |
| `west patch gh-fetch --owner <o> --repo <r> --pull-request <N> --module zephyr` | Fetch an upstream PR as a patch and append its entry (needs `GITHUB_TOKEN`) |

`clean` uses the top-level `clean-command` and `checkout-command` keys in `patches.yml`.

## Rules

- Never edit Zephyr source without a patch entry; changes are lost on `clean` or `update`.
- One logical fix per patch file. Patches apply in filename order.
- `upstreamable: true` when the fix belongs in Zephyr mainline.
- After a Zephyr version bump, re-apply patches and drop those that landed upstream.
