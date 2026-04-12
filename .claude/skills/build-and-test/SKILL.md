---
name: build-and-test
description: Run the mandatory build and test gate for the weather-station project. Invoke after any code, Kconfig, DTS, or config change.
disable-model-invocation: true
---

# Build and Test Gate

Run the mandatory build + test gate for the weather-station project.

## When to use pristine vs. incremental

**Incremental** (only `.c` / `.h` files changed — no Kconfig, no DTS, no `.conf`):
```bash
west build -b native_sim/native/64 apps/gateway
west build -b native_sim/native/64 apps/sensor-node
```

**Pristine** (after any Kconfig, DTS overlay, or `.conf` change):
```bash
west build -p always -b native_sim/native/64 apps/gateway
west build -p always -b native_sim/native/64 apps/sensor-node
```

If in doubt, use pristine — it is slower but always correct.

## Shell smoke-test (run after every build)

```bash
printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Check that:
- `help` lists the `fake_sensors` command (shell + library linked correctly)
- `fake_sensors list` shows all sensors declared in `apps/gateway/boards/native_sim.overlay`
- Startup log shows expected `fake_temperature` / `fake_humidity` init messages

Fix any runtime failure **before** running Twister.

## Full test suite

**CRITICAL:** `ZEPHYR_BASE` in the shell is stale. Always prefix `west twister`:

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
```

This runs both the C-based ztest suites **and** the pytest integration tests.

All tests must be green. Never commit with a red suite.

## Pre-commit check

```bash
pre-commit run --all-files
```

Run this last, immediately before `git commit`.

## Gate order (non-negotiable)

1. Build gateway — fix compile errors first
2. Build sensor-node — fix compile errors
3. Shell smoke-test — fix runtime issues
4. Twister — fix failing tests
5. pre-commit — fix lint / formatting

Do not skip or reorder steps.

## Binary path reference

```
/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe
```
