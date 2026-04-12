---
name: new-integration-test
description: Scaffold a new pytest integration test file under tests/integration/pytest/. Accepts the test topic and markers.
argument-hint: <topic> "<markers>"
---

# Add a New Integration Test File

**Arguments received**: topic=`$0` · markers=`$1` (comma-separated, e.g. "shell,http,e2e")

Creates `tests/integration/pytest/test_<topic>.py` with the correct imports,
markers, and fixture wiring.

---

## Step 1 — Choose which harnesses the tests need

| Fixture | Import from | Use when |
|---------|------------|----------|
| `shell_harness` | conftest (session) | Sending Zephyr shell commands (trigger, list, set values) |
| `http_harness` | conftest (session) | Querying HTTP dashboard API endpoints |
| `mqtt_harness` | conftest (session) | Asserting MQTT messages (auto-skips if no broker) |
| `dut` | twister_harness | Direct DUT interaction via `readlines_until()` |

Tests should use **harness fixtures** (shell_harness, http_harness, mqtt_harness),
not the raw `shell` or `dut` fixture. Harnesses provide parsed, typed results.

---

## Step 2 — Write the test file

File: `tests/integration/pytest/test_<topic>.py`

```python
# SPDX-License-Identifier: Apache-2.0
"""
<Topic> tests.

<One paragraph describing what these tests verify and which subsystems
they exercise.>

Markers:
  <marker1> — <why>
  <marker2> — <why>
"""

import pytest


@pytest.mark.smoke
@pytest.mark.<marker>
def test_<topic>_basic(shell_harness):
    """<Quick sanity check that the feature is alive.>"""
    # Use the appropriate harness method — never send raw shell strings
    result = shell_harness.<method>()
    assert <condition>, f"<helpful message>: {result}"


@pytest.mark.<marker>
def test_<topic>_detailed(shell_harness, http_harness):
    """<Deeper test exercising data flow.>"""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=1)
    assert <condition>
```

---

## Step 3 — Test patterns and conventions

### DUT scope is session
All tests share **one booted gateway instance**. Tests that mutate state
(set sensor values, change trigger interval) must restore defaults in a
finally block or at the end of the test.

```python
def test_set_and_restore(shell_harness):
    shell_harness.set_temperature(0x0001, 30000)
    try:
        sensors = shell_harness.list_sensors()
        assert sensors[...].value_milli == 30000
    finally:
        shell_harness.set_temperature(0x0001, 21000)  # restore
```

### MQTT tests must guard for missing broker
Use the `mqtt_harness` fixture — it auto-skips when no broker is available.
Always add `@pytest.mark.mqtt` so users can opt out with `-m "not mqtt"`.

### Avoid raw time.sleep()
Use polling helpers instead:
- `http_harness.wait_for_readings(min_sensors=N, timeout=T)`
- `mqtt_harness.wait_for_messages(count=N, timeout=T)`

### Assertion messages
Always include a diagnostic message showing the actual value received:
```python
assert uid in by_uid, f"UID {uid:#06x} missing. Found: {sorted(by_uid.keys())}"
```

---

## Step 4 — If you need a new harness method

If the test requires a shell command or API call that the harness doesn't
expose yet, add the method to the appropriate harness class:

| Harness | File |
|---------|------|
| `ShellHarness` | `tests/integration/pytest/harnesses/shell_harness.py` |
| `HttpHarness` | `tests/integration/pytest/harnesses/http_harness.py` |
| `MqttHarness` | `tests/integration/pytest/harnesses/mqtt_harness.py` |

The harness method must:
1. Send the command / make the request
2. Parse the output into a typed Python object (dataclass, dict, int, etc.)
3. Never expose raw strings to the test — that's the whole point of the POM

---

## Step 5 — Validate

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N

pre-commit run --all-files
```

---

## Existing test files for reference

| File | Markers | Tests |
|------|---------|-------|
| `test_shell.py` | smoke, shell | help, uptime, sensor list, initial values, set/trigger |
| `test_http_api.py` | smoke, http | endpoint reachability, JSON schema, sensor UIDs, config POST |
| `test_sensor_flow.py` | e2e, shell, http, mqtt | trigger→HTTP, trigger→MQTT, value propagation |
| `test_config.py` | http, shell | trigger interval, SNTP resync, config_cmd in help |

---

## Common mistakes to avoid

- **Do not** import from `twister_harness` in test files — use the harness
  fixtures from conftest.py instead.
- **Do not** hardcode sensor UIDs without a comment mapping them to the DT
  overlay names (e.g. `0x0001  # fake_temp_indoor`).
- **Do not** use `time.sleep()` for synchronisation — use the harness polling
  methods with a timeout.
- **Do not** leave sensor values modified after a test — always restore.
- **Do not** add `conftest.py` inside `pytest/` subdirectories — the root
  conftest at `tests/integration/pytest/conftest.py` already wires everything.
