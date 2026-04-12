---
name: run-integration-tests
description: Run the pytest integration test suite for the weather-station gateway. Optionally filter by marker (smoke, shell, http, mqtt, e2e).
argument-hint: "[marker]"
allowed-tools: Bash
---

# Run Integration Tests

**Argument received** (optional): marker=`$0`

The integration test suite lives at `tests/integration/`. It uses Twister's
`harness: pytest` to boot the full gateway app on `native_sim/native/64`,
then runs Python tests that interact via shell (UART), HTTP (`localhost:8080`),
and MQTT (`localhost:1883`).

## CRITICAL: ZEPHYR_BASE override

The shell's `ZEPHYR_BASE` is stale. **Every** `west twister` invocation must
be prefixed:

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister ...
```

`west build` is NOT affected — only `west twister`.

---

## Run all integration tests

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N
```

## Run by marker

If argument `$0` is provided (one of: `smoke`, `shell`, `http`, `mqtt`, `e2e`):

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N \
  --pytest-args="-m $0"
```

## Run a single test file

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N \
  --pytest-args="-k test_file_name"
```

## Run all tests (unit + integration)

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/ \
  --inline-logs -v -N
```

---

## Available markers

| Marker | Meaning |
|--------|---------|
| `smoke` | Quick sanity checks — run first |
| `shell` | Tests using Zephyr shell interaction |
| `http`  | Tests using HTTP dashboard API |
| `mqtt`  | Tests using MQTT broker (skipped if no broker) |
| `e2e`   | Full end-to-end data-flow tests |

Multiple markers: `--pytest-args="-m 'smoke or http'"`

---

## Interpreting failures

1. **Build failure** → check `prj.conf` or `boards/native_sim_native_64.overlay`
   in `tests/integration/`. The integration test builds the same stack as the
   gateway app minus LVGL.
2. **Shell fixture timeout ("Prompt not found")** → verify
   `CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y` is in `testcase.yaml extra_configs`.
3. **HTTP connection refused** → the gateway's HTTP server needs ~1s after boot
   to bind port 8080. Use `http_harness.wait_for_readings()` instead of raw
   `get_sensor_data()` to add built-in polling.
4. **MQTT tests skipped** → `paho-mqtt` not installed or Mosquitto not running.
   Install: `pip install paho-mqtt`. Start broker: `mosquitto -p 1883 -d`.
5. **Regex parse failures in ShellHarness** → the shell output format changed.
   Fix the regex in `tests/integration/pytest/harnesses/shell_harness.py`.

---

## Do NOT

- Do not pass `--build-only` for integration tests — the pytest phase is the
  entire point.
- Do not omit the `ZEPHYR_BASE` prefix — `west twister` will crash with
  `ModuleNotFoundError: No module named 'twisterlib'`.
- Do not run integration tests with `pytest` directly — Twister manages the
  build, the DUT lifecycle, and the `twister_harness` plugin wiring.
