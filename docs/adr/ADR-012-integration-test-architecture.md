# ADR-012 — Pytest Integration Test Architecture

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-04-11 |
| **Deciders** | Project founder |
| **Relates to** | [ADR-009](ADR-009-native-sim-first.md), [ADR-010](ADR-010-ci-and-dev-environment.md) |

---

## Context

The project has five C-based ztest suites that test individual libraries in
isolation (fake_sensors, mqtt_publisher, sensor_event, etc.). These validate
internal APIs but cannot catch integration failures across subsystem
boundaries — e.g. a shell trigger that publishes on zbus but never reaches
the HTTP ring-buffer, or an MQTT payload whose JSON schema drifts from what
the broker expects.

The gap: **no test boots the full gateway stack and verifies data flows
end-to-end across shell → zbus → HTTP → MQTT.**

Requirements for the integration test layer:

1. Boot the real gateway app (all SYS_INITs, all zbus channels wired).
2. Interact through the same surfaces a user would: UART shell, HTTP API,
   MQTT topics.
3. Run on `native_sim` in CI — no hardware, no emulator.
4. Transition to hardware (HIL) later without rewriting tests.
5. Make adding a new test trivially easy for anyone who knows Python.

---

## Decision

Use Zephyr's **`pytest-twister-harness`** plugin to delegate the test
execution phase from Twister to pytest. Twister builds the gateway binary
and manages the DUT lifecycle; pytest runs the actual test logic in Python.

### Test topology

```
                    Twister
                       │
            ┌──────────┴──────────┐
            │ 1. Build gateway    │
            │    (native_sim)     │
            │ 2. Launch binary    │
            │    (stdin/stdout)   │
            └──────────┬──────────┘
                       │ DeviceAdapter (process pipes)
                       │
            ┌──────────┴──────────┐
            │      pytest         │
            │                     │
            │  ┌───────────────┐  │
            │  │ ShellHarness  │──┼── UART (stdin/stdout)
            │  ├───────────────┤  │
            │  │ HttpHarness   │──┼── HTTP (localhost:8080)
            │  ├───────────────┤  │
            │  │ MqttHarness   │──┼── MQTT (localhost:1883)
            │  └───────────────┘  │
            │                     │
            │  test_shell.py      │
            │  test_http_api.py   │
            │  test_sensor_flow.py│
            │  test_config.py     │
            └─────────────────────┘
```

### Page Object Model

Tests never send raw shell strings or construct HTTP URLs. Each interaction
surface is wrapped in a **harness class** (the embedded equivalent of a
Page Object):

| Harness | Wraps | Key methods |
|---------|-------|-------------|
| `ShellHarness` | `twister_harness.Shell` | `list_sensors()`, `trigger_all()`, `set_temperature()`, `get_uptime_ms()` |
| `HttpHarness` | `requests` | `get_sensor_data()`, `wait_for_readings()`, `set_trigger_interval()` |
| `MqttHarness` | `paho-mqtt` | `connect()`, `wait_for_messages()`, `clear()`, `topics()` |

Harnesses return typed Python objects (dataclasses, dicts, ints). If a shell
output format changes, only the harness parser needs updating — all tests
remain stable.

### Markers

Tests are tagged with pytest markers for selective execution:

| Marker | Purpose |
|--------|---------|
| `smoke` | Quick sanity (shell alive, HTTP reachable) — run first |
| `shell` | Uses UART shell |
| `http` | Uses HTTP dashboard API |
| `mqtt` | Uses MQTT broker (auto-skip if unavailable) |
| `e2e` | Full pipeline: trigger → zbus → HTTP/MQTT |

### DUT lifecycle

`pytest_dut_scope: session` — the gateway boots **once** per test run. All
tests share the same process. Tests that mutate state (set sensor values,
change trigger interval) must restore defaults before returning.

### Build configuration

The integration test has its own `CMakeLists.txt`, `prj.conf`, `src/main.c`,
and board overlay under `tests/integration/`. This mirrors the gateway stack
but disables LVGL (the display blocks stdin/stdout) and enables
`CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y` so the `DeviceAdapter` can interact
via process pipes.

### Directory layout

```
tests/integration/
├── testcase.yaml                      # harness: pytest, dut_scope: session
├── CMakeLists.txt                     # standard Zephyr app
├── prj.conf                           # gateway stack minus LVGL
├── src/main.c                         # LOG_MODULE_REGISTER + k_sleep(K_FOREVER)
├── boards/native_sim_native_64.overlay # 6 fake sensors, no SDL
└── pytest/
    ├── conftest.py                    # markers, fixture wiring
    ├── harnesses/
    │   ├── shell_harness.py
    │   ├── http_harness.py
    │   └── mqtt_harness.py
    └── test_*.py                      # test files
```

---

## Consequences

**Easier:**

- Adding a new integration test = one Python function with a marker and a
  harness fixture. No C, no CMake, no Kconfig.
- Same tests run on `native_sim` today and on hardware tomorrow — the
  `DeviceAdapter` abstracts the transport (process pipes vs. serial port).
- MQTT tests degrade gracefully — `MqttHarness.connect()` returns `False`
  when no broker is running, and the fixture skips automatically.
- CI runs the full suite alongside existing ztests in one `west twister`
  invocation.

**Harder:**

- Session-scoped DUT means tests are not fully isolated. A test that crashes
  the gateway or corrupts state will fail all subsequent tests.
- Shell output parsing is brittle — if a `shell_print()` format changes in C,
  the Python regex in the harness breaks silently until the next test run.
- `paho-mqtt` and `requests` are runtime dependencies not shipped with Zephyr.
  They must be present in the devcontainer / CI image.

**Constrained:**

- LVGL must be disabled in the integration test build — `lvgl_display_run()`
  takes over the main thread and blocks stdin/stdout.
- The HTTP server needs ~1s after boot to bind port 8080. Tests must use
  `wait_for_readings()` instead of immediate `get_sensor_data()` after a
  trigger.
- Twister's `--pytest-args` is the only way to pass flags to pytest. No
  `pytest.ini` or `pyproject.toml` should be added — Twister owns the
  pytest invocation.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Robot Framework (Renode style) | Heavier syntax; no native Twister integration; Python ecosystem is richer for HTTP/MQTT |
| Pure ztest for integration | C-based tests cannot easily interact with HTTP endpoints or MQTT brokers; test setup is verbose |
| pytest without Twister (standalone) | Loses Twister's build orchestration, platform matrix, and JUnit reporting; manual DUT lifecycle |
| Separate test binary per subsystem | Defeats the purpose — integration tests must boot the full stack to catch cross-subsystem bugs |
