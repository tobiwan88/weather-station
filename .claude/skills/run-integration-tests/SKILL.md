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

## CRITICAL: pre-flight checks

Before running, verify:

```bash
# 1. Start Mosquitto if you want MQTT-marked tests to run.
#    Otherwise those tests are skipped; the DUT does not exit at boot —
#    the MQTT publisher thread keeps retrying the broker in the background.
mosquitto -p 1883 -d 2>/dev/null || true
# verify:
netstat -tlnp 2>/dev/null | grep 1883 || ss -tlnp 2>/dev/null | grep 1883

# 2. ZEPHYR_BASE must be set explicitly (see below)
```

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

## Run a single test by name (most useful for debugging)

Use `-k <test_name>` to run exactly one test function. This is the fastest
way to iterate on a failing test — Twister still builds once and boots one DUT,
but pytest only executes the matched test.

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N \
  --pytest-args='-k test_post_config_without_token_returns_401'
```

`-k` matches on the test function name (substring or exact). Examples:

```bash
# exact name
--pytest-args='-k test_token_rotation_invalidates_old_token'

# substring — runs all tests whose name contains "token"
--pytest-args='-k token'

# combine with a marker
--pytest-args='-m http -k rotation'
```

> **Tip for debugging crashes:** run the single failing test in isolation so
> the DUT log (`handler.log`) and pytest output (`twister_harness.log`) contain
> only that test's traffic, making the crash signal much easier to spot.
> Log files are at:
> `twister-out/native_sim_native_64/host/weather-station/tests/integration/weather_station.integration/`

## Run a single test file

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N \
  --pytest-args="-k test_http_api"
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

2. **DUT exits after only a few boot messages ("No connection to the device" for all tests)** →
   Mosquitto is not running. The MQTT publisher fails to connect and the DUT exits at boot.
   Fix: `mosquitto -p 1883 -d`

3. **`handler.log` ends with `error in EPOLL_CTL_ADD: errno=17`** →
   NSOS epoll race: a background thread (SNTP, remote scan, etc.) opened a socket
   concurrently with the HTTP server's in-flight response, and both called
   `epoll_ctl ADD` on the same fd at the same time.
   Fix: add `k_sleep(K_MSEC(CONFIG_..._PRESYNC_DELAY_MS))` in the triggered path
   of the offending background thread (after sem/signal wake, before `zsock_socket()`).
   Also verify `CONFIG_ZVFS_POLL_MAX≥8` and `CONFIG_NET_MAX_CONTEXTS≥16` in `prj.conf`.

4. **First N tests pass then all remaining tests fail ("No connection")** →
   DUT exited mid-suite. Inspect `handler.log` tail for the crash message, then
   follow item 3 above.

5. **Shell fixture timeout ("Prompt not found")** → verify
   `CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y` is in `testcase.yaml extra_configs`.

6. **HTTP connection refused** → the gateway's HTTP server needs ~1s after boot
   to bind port 8080. Use `http_harness.wait_for_readings()` instead of raw
   `get_sensor_data()` to add built-in polling.

7. **MQTT tests skipped** → `paho-mqtt` not installed or Mosquitto not running.
   Install: `pip install paho-mqtt`. Start broker: `mosquitto -p 1883 -d`.

8. **Regex parse failures in ShellHarness** → the shell output format changed.
   Fix the regex in `tests/integration/pytest/harnesses/shell_harness.py`.

9. *** Exten debugging ***
   For better debugging increase the log level of involved modules
   e.g. CONFIG_NET_HTTP_SERVER_LOG_LEVEL_DBG=y in prj.conf
   do not increase the generic log level!

---

## Do NOT

- Do not pass `--build-only` for integration tests — the pytest phase is the
  entire point.
- Do not omit the `ZEPHYR_BASE` prefix — `west twister` will crash with
  `ModuleNotFoundError: No module named 'twisterlib'`.
- Do not run integration tests with `pytest` directly — Twister manages the
  build, the DUT lifecycle, and the `twister_harness` plugin wiring.
- Do not increase in the prj.conf CONFIG_LOG_DEFAULT_LEVEL=4 to many messages
