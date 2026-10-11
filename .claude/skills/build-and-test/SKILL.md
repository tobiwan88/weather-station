---
name: build-and-test
description: Run the mandatory build, twister, pytest and pre-commit gate after any code, Kconfig, DTS or config change, before commit. Use for: build, run tests, twister, check standards.
argument-hint: "[gate | twister <suite> | pytest <marker>]"
allowed-tools: Bash
---

# Build and Test

Run every command through `scripts/ws.sh`, from the workspace root. `weather-station/` is the
repo path inside it. Don't claim success without running the command.

## Full gate (run before every commit)

```bash
scripts/ws.sh 'cd weather-station && scripts/build-gate.sh'
```

Runs, in order: build gateway, build sensor-node, shell smoke test, twister, pre-commit.
Progress goes to stderr; JSON with per-step results goes to stdout. Exit 0 = every executed step passed, 1 = a step failed. `-T tests/<suite>` limits twister to one suite.

Options: `--pristine` (force rebuild; auto-detected from git diff), `--skip-smoke`,
`--skip-tests`, `--skip-precommit`, `--sca` (CodeChecker, informational).

## Individual steps

```bash
scripts/ws.sh 'west build -p -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'
scripts/ws.sh 'west build -p -b native_sim/native/64 -d build/native_sim_native_64/sensor-node weather-station/apps/sensor-node'
scripts/ws.sh 'printf "help\nfake_sensors list\n" | timeout 10 build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout'
scripts/ws.sh 'cd weather-station && west twister -p native_sim/native/64 -T tests/ -N -j 8'
scripts/ws.sh 'cd weather-station && pre-commit run --all-files'
```

Use `-p` (pristine) after any Kconfig, DTS or `.conf` change. Without it, incremental builds
can keep stale config.

## Single twister suite

```bash
scripts/ws.sh 'cd weather-station && west twister -p native_sim/native/64 -T tests/<suite> -N -j 8 --inline-logs -v'
```

Replace `<suite>` with a directory under `tests/`. Add `--inline-logs -v` when debugging a failure.

## Single pytest marker (integration tests)

```bash
scripts/ws.sh 'cd weather-station && scripts/run-tests.sh --marker smoke'
scripts/ws.sh 'cd weather-station && scripts/run-tests.sh --test test_http_api'
```

Markers: `smoke` (run first), `shell`, `http`, `mqtt`, `e2e`, `system`. `--all` runs unit and
integration tests. Don't run `pytest` directly; twister manages the DUT and the harness.

## Standards check

```bash
scripts/ws.sh 'cd weather-station && scripts/check-standards.sh master..HEAD'
```

No argument checks uncommitted changes. Also accepts `HEAD~1..HEAD` or file names after `--`.
Exit 0 = PASS, 1 = BLOCK (fix before commit), 2 = WARN.

## Gotchas

- **MQTT tests skip without a broker.** Start one with `scripts/ws.sh 'mosquitto -p 1883 -d'`.
  Install `mosquitto` in the container if it is missing.
- **Stale builds.** If a build fails with a stale path, delete `CMakeCache.txt` in that build dir and rebuild with `-p`.
- **Stale ZEPHYR_BASE.** `ws.sh` sets it. Don't call `west` from the host shell.
- **No `printk`, heap or banned string functions** in `lib/` or `apps/`. The standards check catches these.
- **Test failure or DUT crash:** run `/systematic-debug` before changing code.
- **No container:** if `ws.sh` reports no build environment, start the `ws_build` container
  (see the repo CLAUDE.md).
