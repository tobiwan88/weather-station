# Integration Test Architecture

## Purpose

Unit tests (ztest) verify each library in isolation. Integration tests verify
the **assembled gateway** — that a shell trigger propagates through zbus,
populates the HTTP ring-buffer, and arrives at the MQTT broker with the
correct topic and payload. The test binary boots the full stack minus LVGL.

Decision rationale: [ADR-012](../adr/ADR-012-integration-test-architecture.md).

---

## Execution Model

```
  west twister -T tests/integration
        │
        ├─ 1. Build  tests/integration/ as a Zephyr app (CMake + prj.conf)
        ├─ 2. Launch  zephyr.exe as child process (stdin/stdout = UART)
        └─ 3. Invoke  pytest with twister_harness plugin
                │
                ├─ DeviceAdapter (process pipes)
                │     └─ shell fixture ─── ShellHarness
                ├─ HTTP (localhost:8080) ── HttpHarness
                └─ MQTT (localhost:1883) ── MqttHarness (optional)
```

Twister owns the build and DUT lifecycle. Pytest owns the assertions.
`CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y` routes the Zephyr shell to
stdin/stdout so the `DeviceAdapter` reads/writes via process pipes.

The DUT scope is **session** — one boot per run. All tests share the
same gateway process.

---

## Test Surfaces

| Surface | Transport | Harness | Auto-available |
|---------|-----------|---------|----------------|
| UART shell | stdin/stdout pipes | `ShellHarness` | yes |
| HTTP dashboard | TCP localhost:8080 | `HttpHarness` | yes |
| MQTT broker | TCP localhost:1883 | `MqttHarness` | no — skips if no broker |

### ShellHarness

Wraps `twister_harness.Shell`. Sends shell commands, parses output into
typed Python objects.

```
test code                    ShellHarness                   DUT (Zephyr shell)
─────────                    ────────────                   ──────────────────
list_sensors()          ──►  exec_command("fake_sensors list")  ──►  shell_print()
  returns list[SensorEntry]  ◄──  regex parse table rows        ◄──  stdout lines
```

Key methods:

| Method | Shell command | Returns |
|--------|---------------|---------|
| `list_sensors()` | `fake_sensors list` | `list[SensorEntry]` (uid, kind, location, value_milli, unit) |
| `trigger_all()` | `fake_sensors trigger` | confirmation string |
| `set_temperature(uid, mdegc)` | `fake_sensors temperature_set <uid> <val>` | — |
| `set_humidity(uid, mpct)` | `fake_sensors humidity_set <uid> <val>` | — |
| `set_co2(uid, mppm)` | `fake_sensors co2_set <uid> <val>` | — |
| `set_voc(uid, miaq)` | `fake_sensors voc_set <uid> <val>` | — |
| `get_uptime_ms()` | `kernel uptime` | `int` |
| `get_version()` | `kernel version` | `str` |

### HttpHarness

Wraps `requests`. Calls HTTP endpoints and returns parsed JSON.

| Method | Endpoint | Returns |
|--------|----------|---------|
| `wait_until_ready(timeout)` | `GET /` (polls) | — raises `TimeoutError` |
| `get_sensor_data()` | `GET /api/data` | `dict` (sensors + readings) |
| `wait_for_readings(min_sensors, timeout)` | `GET /api/data` (polls) | `dict` or raises `TimeoutError` |
| `get_config()` | `GET /api/config` | `dict` |
| `post_config(data)` | `POST /api/config` | `dict` (JSON response) |
| `set_trigger_interval(ms)` | `POST /api/config` | HTTP status code |
| `request_sntp_resync()` | `POST /api/config` | HTTP status code |
| `get_locations()` | `GET /api/locations` | `dict` |
| `get_dashboard_page()` | `GET /` | HTML string |
| `get_token_from_shell(shell_harness)` | — | bearer token string |

### MqttHarness

Wraps `paho-mqtt`. Subscribes to `weather/#`, collects messages in a
thread-safe list.

| Method | Purpose |
|--------|---------|
| `connect(topic)` | Connect + subscribe. Returns `False` if broker unreachable |
| `wait_for_messages(count, timeout)` | Block until N messages or timeout |
| `messages()` | Snapshot of `list[MqttMessage]` (topic + payload dict) |
| `topics()` | Unique topics seen |
| `clear()` | Reset collected messages |

The conftest fixture calls `connect()` and skips the entire MQTT test
subset if it returns `False`.

### DeviceLogger

Drains the DUT's UART queue and re-emits structured Python log records under
the `"device"` logger. Parses Zephyr's `[HH:MM:SS.mmm,mmm] <level> module: message`
format, stripping ANSI escape codes. Used by conftest to detect the boot-complete
sentinel and to drain logs after each test.

| Method | Purpose |
|--------|---------|
| `drain()` | Return `list[ZephyrLogEntry]` and clear the buffer |
| `wait_for_ready(timeout)` | Block until `"device: ready"` log line appears |

The `device_ready` fixture depends on `DeviceLogger.wait_for_ready()` to guarantee
all `SYS_INIT` callbacks have completed before any test runs.

---

## Markers

```bash
# Run only smoke tests
--pytest-args="-m smoke"

# Run everything except MQTT
--pytest-args="-m 'not mqtt'"

# Combine
--pytest-args="-m 'shell and not e2e'"
```

| Marker | Meaning | Typical count |
|--------|---------|---------------|
| `smoke` | Shell alive, HTTP reachable — fast gate | 3-4 tests |
| `shell` | Uses ShellHarness | majority |
| `http` | Uses HttpHarness | ~10 tests |
| `mqtt` | Uses MqttHarness (requires broker) | 3-4 tests |
| `e2e` | Full pipeline across multiple surfaces | 5-6 tests |
| `system` | Multi-process tests (sensor-node + gateway) | 1-2 tests |

---

## File Layout

```
tests/integration/
├── testcase.yaml           ← harness: pytest, dut_scope: session
├── CMakeLists.txt          ← find_package(Zephyr), target_sources(app src/main.c)
├── prj.conf                ← gateway stack, no LVGL, NSOS networking
├── src/main.c              ← LOG_MODULE_REGISTER + k_sleep(K_FOREVER)
├── boards/
│   └── native_sim_native_64.overlay  ← 6 fake sensors, no SDL
└── pytest/
    ├── conftest.py         ← markers, fixture wiring, DeviceLogger setup
    ├── harnesses/
    │   ├── __init__.py
    │   ├── device_logger.py   ← drains UART, parses Zephyr log format
    │   ├── log_parser.py      ← ZephyrLogEntry dataclass, ANSI strip
    │   ├── shell_harness.py
    │   ├── http_harness.py
    │   └── mqtt_harness.py
    ├── test_shell.py          ← [smoke, shell] help, sensors, uptime, set/trigger
    ├── test_http_api.py       ← [smoke, http] endpoints, JSON schema, config POST
    ├── test_config.py         ← [http, shell] trigger interval, SNTP resync
    ├── test_sensor_flow.py    ← [e2e] trigger→HTTP, trigger→MQTT, value propagation
    ├── test_mqtt_config.py    ← [http, shell, mqtt, e2e] MQTT runtime config via HTTP/shell
    └── test_sensor_node_gateway.py ← [system, http, shell] sensor-node subprocess → FIFO → gateway
```

---

## Conftest Fixtures

| Fixture | Scope | Purpose |
|---------|-------|---------|
| `device_logger` | session | `DeviceLogger` instance; drains UART after every test (autouse) |
| `device_ready` | session | Blocks until shell prompt; guarantees all `SYS_INIT` callbacks complete |
| `shell_harness` | session | `ShellHarness` instance (depends on `device_ready`) |
| `http_harness` | session | `HttpHarness` instance with `wait_until_ready()` called (depends on `device_ready`) |
| `authed_harness` | session | `http_harness` with bearer token loaded via `get_token_from_shell()`; for authenticated `POST /api/config` tests when auth is enabled |
| `mqtt_harness` | session | `MqttHarness` instance; auto-skips test if broker unreachable |
| `gdb_crash_watcher` | session | Optional; attaches GDB to the DUT process and captures a backtrace on crash |

---

## Build Configuration Delta vs Gateway

The integration test `prj.conf` matches `apps/gateway/prj.conf` with these
differences:

| Config | Gateway | Integration test | Why |
|--------|---------|------------------|-----|
| `CONFIG_LVGL_DISPLAY` | `y` | omitted (n) | LVGL blocks stdin/stdout |
| `CONFIG_LVGL` | `y` | omitted (n) | not needed without display |
| `CONFIG_DISPLAY` | `y` | omitted (n) | no SDL display node |
| `CONFIG_INPUT` | `y` | omitted (n) | no touch/keyboard input |
| `CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT` | unset | `y` (via extra_configs) | routes shell to process pipes |

The board overlay omits the `&sdl_dc` node and `zephyr,display` chosen.

---

## Data Flow Under Test

The most valuable integration test pattern — **trigger → verify at endpoint**:

```
test_sensor_flow.py
        │
        │  shell_harness.set_co2(0x0003, 1500000)   ← set distinctive value
        │  shell_harness.trigger_all()                ← fire broadcast trigger
        │                                                    │
        │                                      ┌─────────────┘
        │                                      ▼
        │                           sensor_trigger_chan
        │                                      │
        │                        ┌─────────────┼─────────────┐
        │                        ▼             ▼             ▼
        │                   fake_temp     fake_co2     fake_hum ...
        │                        │             │             │
        │                        └─────────────┼─────────────┘
        │                                      ▼
        │                           sensor_event_chan
        │                                      │
        │                        ┌─────────────┼──────────────┐
        │                        ▼             ▼              ▼
        │                  http_dashboard  mqtt_publisher  event_log
        │                        │             │
        │  ◄─────────────────────┘             └──────────────────► broker
        │  http_harness.wait_for_readings()     mqtt_harness.wait_for_messages()
        │  assert co2 ≈ 1500 ppm               assert topic startswith "weather/"
```

---

## Session Isolation Rules

Since all tests share one DUT process:

1. **Restore state.** If a test sets a sensor value or trigger interval,
   restore the original before returning.
2. **No crash-inducing inputs.** Don't send malformed shell commands that
   could hang the parser.
3. **Ordering tolerance.** Tests must not depend on execution order. Use
   `wait_for_readings()` / `wait_for_messages()` instead of `time.sleep()`.
4. **Idempotent assertions.** The HTTP ring-buffer accumulates readings.
   Assert on the *latest* reading or on *at least N* readings, not on an
   exact count.

---

## Adding a New Test

1. Create `tests/integration/pytest/test_<topic>.py`.
2. Import fixtures from conftest: `shell_harness`, `http_harness`, `mqtt_harness`.
3. Add markers: `@pytest.mark.shell`, `@pytest.mark.e2e`, etc.
4. Use harness methods — never raw `shell.exec_command()` strings.
5. If a new shell command or endpoint needs wrapping, add a method to the
   corresponding harness class.
6. Run: `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N`

See skills `/new-integration-test` and `/new-harness` for scaffolding.

---

## HIL Transition Path

When hardware arrives (Phase 3), the same tests run on a real board:

```bash
west twister --device-testing --device-serial /dev/ttyACM0 \
  -p <board> -T tests/integration --inline-logs -v
```

Changes needed:

- `testcase.yaml`: add the board to `platform_allow`
- Board overlay: real sensors instead of fake ones
- `prj.conf`: hardware-specific Kconfig (real drivers, no NSOS)
- `HttpHarness`: point `base_url` to the board's IP
- `MqttHarness`: point to the board's broker or a test broker

Test logic (the `test_*.py` files) stays **unchanged**.
