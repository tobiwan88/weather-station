---
name: systematic-debug
description: Use for any Zephyr build failure, twister test failure, native_sim crash or unexpected runtime behavior, before proposing a fix. Triggers: "debug", "crash", "build fail", "test fail", "EEXIST", "EPOLL".
allowed-tools: Read, Bash, Grep, Glob
---

# Systematic Debugging for Zephyr

**Core rule:** find the root cause before changing code. A symptom fix hides the bug and usually creates another.

Run all build and test commands through `scripts/ws.sh`. It sets `ZEPHYR_BASE` and runs from the west workspace root, so paths are relative to that root:

```bash
scripts/ws.sh 'west build -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'
scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/ --inline-logs -v -N'
```

## Phase 1: Investigate

1. **Reproduce.** Run the failing command again, unchanged. If it does not fail the same way every time, gather more evidence before forming a theory.
2. **Read the actual error.** Read the whole output: the first failure in the CMake or compiler log, the full stack trace, the twister log. The first error is usually the cause; later ones are often fallout.
3. **Triage** the symptom with the table below.
4. **Check recent changes:** `git log --oneline -5` and `git diff --stat HEAD`.

For a failure deep in a chain (build, then twister, then test, then assertion), add temporary diagnostics at each boundary, run once, and find where the data goes wrong. Remove them afterwards.

### Triage table

| Symptom | Likely cause | Action |
|---|---|---|
| Kconfig or DTS error, or a symbol or node that should exist is missing | Stale build output | Rebuild pristine: `west build -p always ...` |
| A Kconfig or CMake value ignores your change | Stale `CMakeCache.txt` | Delete it from the build dir, or rebuild with `-p always` |
| `undefined reference to __device_dts_ord_*` | Missing DT node or overlay | Check `weather-station/apps/gateway/boards/native_sim.overlay` |
| Zephyr SDK version mismatch | SDK out of date | Update the SDK in the container |
| `west update` fetch failure | Manifest allowlist changed | `scripts/ws.sh 'west update --narrow'` |
| Upstream Zephyr behavior differs from expected | Patches not applied | `scripts/ws.sh 'west patch apply'`, then rebuild |
| `EPOLL_CTL_ADD` errno 17 (EEXIST) crash | Two threads opened sockets at once | Follow `.claude/rules/native-sim-net.md` (presync delay) |
| Socket or poll exhaustion (`ZVFS_POLL_MAX`, `NET_MAX_CONTEXTS`) | Too few slots for HTTP, MQTT, SNTP | Check the limits in `.claude/rules/native-sim-net.md` |
| Twister timeout | Binary hang, or LVGL enabled in a test build | Confirm LVGL is off; check the poll limits |
| MQTT tests skipped or failing | No broker on port 1883 | Start a local broker on that port |
| Fault inside a zbus listener | Listener blocks or sleeps | Move the work to a subscriber thread; see `.claude/rules/firmware.md` |
| `__ASSERT` failure | Caller passed bad input | Find the caller; never assert on external input |

### Read the twister logs

Each suite writes its logs under `twister-out/` in the workspace root. For a failing suite, read:

- `build.log`: compile, link and Kconfig errors
- `handler.log`: runtime output and the failing assertion

```bash
scripts/ws.sh 'find twister-out -name handler.log -o -name build.log'
scripts/ws.sh 'cat twister-out/native_sim_native_64/<suite-path>/handler.log'
```

## Phase 2: Compare with a working example

- Find code in `lib/` that does the same job and works. List every difference from the broken code; do not assume a difference "can't matter".
- For subsystem behavior, check the matching Zephyr sample under `zephyr/samples/`.

## Phase 3: One hypothesis, one change

1. State it precisely: "X is the root cause because Y."
2. Test it with the smallest change that can confirm or refute it. One variable at a time.
3. If it fails, form a new hypothesis. Do not stack fixes on the last attempt.
4. After 3 failed fixes, stop and escalate to the user. The architecture or assumptions may be wrong.

## Phase 4: Fix and verify

1. Make one fix that addresses the root cause. No unrelated refactoring.
2. Verify with the `build-and-test` gate. Do not claim the bug is fixed without a fresh passing run.
