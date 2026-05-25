---
name: build-and-test
description: Use after any code, Kconfig, DTS, or config change — before committing. Runs the mandatory build + test gate for the weather-station project.
allowed-tools: Bash
disable-model-invocation: false
---

# Build and Test Gate

Run the mandatory build + test gate for the weather-station project.

## Verification Gate (non-negotiable)

Before claiming any step passes:
1. **IDENTIFY** what output proves it (exit code 0, "BUILD SUCCESS", "0 failed")
2. **RUN** the full command fresh — never trust a previous run
3. **READ** the full output, scan for errors, count failures
4. **VERIFY** the output matches the success condition
5. **ONLY THEN** claim the step passed and move to the next

Never claim: "should pass", "probably fine", "looks correct", "built earlier."
Each step's evidence must come from THIS invocation. If a step fails, fix it
BEFORE moving on — do not proceed with known failures.

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

**Mosquitto is only required to run MQTT-marked integration tests.** Without a
broker, those tests are skipped and the DUT continues normally — the MQTT
publisher thread retries the connection in the background. Start it if you want
full MQTT coverage:

```bash
mosquitto -p 1883 -d 2>/dev/null || true
```

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

## Red Flags — STOP and re-run the failed step

| Feeling | Reality |
|---------|---------|
| "Should pass" / "probably fine" / "looks correct" | You didn't verify |
| Trusting a build from an earlier session | Not fresh evidence |
| Skipping sensor-node build because "only gateway changed" | Build both |
| Skipping shell smoke-test because "just a small change" | Small changes can break init |
| Proceeding past a test failure saying "will fix later" | Fix now or don't proceed |
| Proceeding past a pre-commit hook failure | Fix now or don't proceed |
| Any wording implying success without having RUN the verification command | No evidence = no claim |

## Gotchas

- **ZEPHYR_BASE is stale.** Always prefix `west twister` with `ZEPHYR_BASE=/home/zephyr/workspace/zephyr`. `west build` is NOT affected.
- **Pristine vs. incremental.** Kconfig/DTS/conf changes require `-p always`. When in doubt, pristine is always safe.
- **Mosquitto.** MQTT tests skip silently if no broker is running. Start `mosquitto -p 1883 -d` for full coverage.
- **CMakeCache.txt.** If builds fail with a stale path, delete `CMakeCache.txt` and rebuild.

## Binary path reference

```
/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe
```
