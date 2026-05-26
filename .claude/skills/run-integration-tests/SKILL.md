---
name: run-integration-tests
description: Use when running the pytest integration test suite against a built gateway. Optionally filter by marker. Invoke when the user says "run tests", "run integration tests", "test gateway", or "twister".
argument-hint: "[marker]"
allowed-tools: Bash
---

# Run Integration Tests

**Argument received** (optional): marker=`$0`

The integration test suite lives at `tests/integration/`. It uses Twister's
`harness: pytest` to boot the full gateway app on `native_sim/native/64`,
then runs Python tests that interact via shell (UART), HTTP (`localhost:8080`),
and MQTT (`localhost:1883`).

## Use the script

Instead of constructing twister commands manually, use the test runner script:

```bash
scripts/run-tests.sh [options]
```

**Options:**
- `--marker MARKER` — Run tests matching marker (smoke|shell|http|mqtt|e2e|system)
- `--test NAME` — Run a single test by name (substring match)
- `--all` — Run all tests (unit + integration)
- `--skip-mosquitto` — Skip mosquitto pre-flight (MQTT tests will be skipped)
- `--quiet` — Suppress stderr progress output

**Examples:**
```bash
scripts/run-tests.sh                    # all integration tests
scripts/run-tests.sh --marker smoke     # smoke tests only
scripts/run-tests.sh --marker http      # HTTP tests only
scripts/run-tests.sh --test test_http_api  # single test file
scripts/run-tests.sh --all              # unit + integration
```

**Output:** JSON to stdout with test counts, pass/fail/skip, and overall verdict. Human-readable progress goes to stderr.

**JSON output schema:**
```json
{
  "script": "run-tests",
  "timestamp": "2026-05-25T12:00:00Z",
  "verdict": "PASS|FAIL",
  "config": {
    "marker": "smoke",
    "test_filter": "none",
    "test_dir": "tests/integration",
    "mosquitto": "RUNNING|NOT_RUNNING|N/A"
  },
  "results": {
    "passed": 5,
    "failed": 0,
    "skipped": 2,
    "total": 7,
    "duration_s": 45
  },
  "output": "...",
  "summary": "All 5 tests passed."
}
```

**Exit codes:** 0 = all PASS, 1 = FAIL, 2 = partial (some skipped)

## Available markers

| Marker | Meaning |
|--------|---------|
| `smoke`  | Quick sanity checks — run first |
| `shell`  | Tests using Zephyr shell interaction |
| `http`   | Tests using HTTP dashboard API |
| `mqtt`   | Tests using MQTT broker (skipped if no broker) |
| `e2e`    | Full end-to-end data-flow tests |
| `system` | Multi-process tests (sensor-node subprocess → FIFO → gateway) |

Multiple markers: combine with `-m` in pytest args, e.g. `--pytest-args="-m 'smoke or http'"`

## Interpreting failures

1. **Build failure** → check `prj.conf` or `boards/native_sim.overlay`
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

9. **Extended debugging** → For better debugging increase the log level of involved
   modules, e.g. `CONFIG_NET_HTTP_SERVER_LOG_LEVEL_DBG=y` in `prj.conf`.
   Do not increase the generic log level.

## If tests fail

If integration tests fail with a DUT crash or unexpected behavior, invoke `/systematic-debug` to investigate the root cause before attempting fixes.

## Do NOT

- **Do not** pass `--build-only` for integration tests — the pytest phase is the entire point.
- **Do not** omit the `ZEPHYR_BASE` prefix — `west twister` will crash with `ModuleNotFoundError: No module named 'twisterlib'`.
- **Do not** run integration tests with `pytest` directly — Twister manages the build, the DUT lifecycle, and the `twister_harness` plugin wiring.
- **Do not** increase `CONFIG_LOG_DEFAULT_LEVEL=4` in prj.conf — too many messages.
