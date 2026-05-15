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
and manages the DUT lifecycle; pytest runs the actual test logic in Python
via a `DeviceAdapter` that abstracts the transport (process pipes on
native_sim, serial port on hardware). Tests interact through harness classes
(Page Object Model) wrapping each interaction surface (UART shell, HTTP,
MQTT). One DUT instance boots per session; all tests share it.

For the topology diagram, harness class table, markers, DUT lifecycle rules,
build configuration, and directory layout, see
[`docs/architecture/integration-tests.md`](../architecture/integration-tests.md).

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

---

## See also

- Current implementation: [`docs/architecture/integration-tests.md`](../architecture/integration-tests.md) (harness classes, file layout, conftest fixtures, NSOS constraints)
- Current tests: `tests/integration/`
- Related ADRs: [ADR-009](ADR-009-native-sim-first.md) (native_sim first), [ADR-010](ADR-010-ci-and-dev-environment.md) (CI pipeline)
