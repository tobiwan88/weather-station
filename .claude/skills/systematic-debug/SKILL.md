---
name: systematic-debug
description: Use when encountering any Zephyr build failure, twister test failure, native_sim crash, or unexpected runtime behavior — before proposing fixes
---

# Systematic Debugging for Zephyr

## Overview

Random fixes waste time and create new bugs. Quick patches mask underlying issues.

**Core principle:** ALWAYS find root cause before attempting fixes. Symptom fixes are failure.

```
NO FIXES WITHOUT ROOT CAUSE INVESTIGATION FIRST
```

If you haven't completed Phase 1, you cannot propose fixes.

---

## Phase 1: Root Cause Investigation

### 1a. Read error messages completely

Do not skip past errors or warnings. They often contain the exact solution.
Read stack traces, CMake output, twister logs, and handler.log completely.

### 1b. Check known failure patterns

Match the error against these common Zephyr patterns before forming hypotheses:

| Symptom | Likely cause | Fix |
|---|---|---|
| `Zephyr-sdk` version mismatch (e.g. "requires 1.0, found 0.17.4") | SDK not upgraded | `west update` or install SDK 1.0 |
| `ZEPHYR_BASE` points to wrong path | Stale env var | `export ZEPHYR_BASE=/home/zephyr/workspace/zephyr` |
| CMake cache mismatch ("Build directory is for application X, but Y was specified") | Stale CMakeCache.txt | Delete `build/CMakeCache.txt` or use `-p always` |
| `west update` fetch failures | `name-allowlist` changed | Run `west update --narrow` |
| Patches not applied after `west update` | Forgot `west patch apply` | Run `west patch apply` |
| `EPOLL_CTL_ADD: errno=17` (EEXIST) in handler.log | Concurrent epoll registration | Increase `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS`; add `k_sleep` before socket open in background threads |
| `CONFIG_ZVFS_POLL_MAX` exhaustion | HTTP server needs ≥ 5 poll slots | Set `CONFIG_ZVFS_POLL_MAX=8` |
| `CONFIG_NET_MAX_CONTEXTS` exhaustion | HTTP + MQTT + SNTP together need ≥ 16 | Set `CONFIG_NET_MAX_CONTEXTS=16` |
| Twister test timeout | native_sim binary hang | Check if LVGL is enabled (must be `n` in test builds); check `CONFIG_ZVFS_POLL_MAX` |
| MQTT tests skipped or fail | No mosquitto broker | `mosquitto -p 1883 -d 2>/dev/null` |
| Undefined reference to `__device_dts_ord_...` | Missing DT node or overlay | Check `apps/gateway/boards/native_sim.overlay` |
| Kernel panic in zbus listener | Listener blocks/sleeps | Move heavy work to subscriber thread; listeners must not block |
| `__ASSERT` failure | Internal invariant violated | Check calling code for bad parameters; never assert on external input |

### 1c. Reproduce consistently

- Can you trigger the failure reliably?
- Same command, same environment, same outcome?
- If not reproducible → gather more data. Don't guess.

### 1d. Check recent changes

```bash
git diff HEAD~1..HEAD           # what changed?
git log --oneline -5            # recent commits
git diff --stat                 # uncommitted changes
```

### 1e. Trace data flow (multi-component systems)

When the error is deep in a component chain (build → twister → test → assertion):

1. **Add diagnostic output at each boundary**
2. **Run once to gather evidence**
3. **Identify which boundary the data changes at**
4. **Investigate that specific component**

Example for a "sensor event not reaching MQTT" bug:
```
Trigger sensor → check zbus publish succeeded → check MQTT subscriber queue count → check MQTT socket write
```

---

## Phase 2: Pattern Analysis

### 2a. Find working examples

Search cocoindex or the codebase for similar working code:
- Same subsystem, different sensor? Find the sensor that works.
- Same pattern in a different library? Find the reference implementation.
- Same pattern in a Zephyr sample? Read `zephyr/samples/` for that subsystem.

### 2b. Compare against references

```bash
# Find a working example of the same subsystem
grep -r "similar_pattern" lib/ --include="*.c"

# Check Zephyr samples for the subsystem
ls zephyr/samples/subsys/<subsystem>/
```

### 2c. Identify differences

List every difference between the working reference and the broken code.
Don't assume "that can't matter" — list it anyway.

---

## Phase 3: Hypothesis and Testing

### 3a. Form a single hypothesis

"I think X is the root cause because Y."

Be specific. Not: "something is wrong with the config." But: "CONFIG_ZVFS_POLL_MAX=3 is too low because the HTTP server needs 5 poll slots."

### 3b. Test minimally

Make the SMALLEST possible change to test the hypothesis. One variable at a time.

### 3c. Verify before continuing

- Did it work? → Phase 4
- Didn't work? → Form NEW hypothesis. DON'T add more fixes on top.
- After 3 failed fixes: STOP. Question the architecture. Escalate to human.

---

## Phase 4: Implementation

### 4a. Create a failing test case (if applicable)

For runtime bugs: write a minimal ztest or shell command that reproduces.
For build failures: document the exact `west build` command.

### 4b. Implement a single fix

Address the root cause. ONE change. No "while I'm here" refactoring.

### 4c. Verify

Run the build test gate (`/build-and-test`). Don't claim fixed without fresh evidence.

---

## Red Flags — STOP and Follow Process

- "Quick fix for now, investigate later"
- "Just try changing X and see if it works"
- Skipping Phase 1 because "I know what's wrong"
- "It's probably just a config issue"
- Proposing a fix without reading the error message fully
- "One more fix attempt" (after already tried 3)
- "The SDK version mismatch is unrelated" — it's always related
- Trusting a build from before `west patch apply`
- **Any fix proposed before completing Phase 1**

---

## Rules

- Never propose a fix before identifying the root cause
- Never skip Phase 1 because the error "looks familiar"
- After 3 failed fix attempts: STOP. Escalate to human.
- This skill is about finding WHAT is wrong and WHY — NOT about implementing the fix
- For implementation of the fix: follow the project's build-and-test gate
