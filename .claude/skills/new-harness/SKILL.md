---
name: new-harness
description: Add a new Page Object harness class for integration tests. Wraps a new interaction surface (e.g. a new shell module, protocol, or API).
argument-hint: <harness_name> "<description>"
---

# Add a New Integration Test Harness

**Arguments received**: harness_name=`$0` · description=`$1`

Creates a new Page Object Model harness under
`tests/integration/pytest/harnesses/<harness_name>_harness.py` and registers
a session-scoped fixture in `conftest.py`.

---

## When to add a new harness vs. extend an existing one

**Extend** an existing harness when:
- You're adding a new shell sub-command (→ add method to `ShellHarness`)
- You're adding a new HTTP endpoint (→ add method to `HttpHarness`)
- You're adding a new MQTT topic assertion (→ add method to `MqttHarness`)

**Create** a new harness when:
- You're adding a new interaction surface (e.g. CoAP, BLE, WebSocket, a
  new protocol's shell module that deserves its own namespace)
- The existing harness would grow beyond its single-responsibility

---

## Step 1 — Create the harness class

File: `tests/integration/pytest/harnesses/<harness_name>_harness.py`

```python
# SPDX-License-Identifier: Apache-2.0
"""
<HarnessName> harness — <description>.

<One paragraph explaining what interaction surface this harness wraps,
which Zephyr subsystem it targets, and how tests should use it.>
"""

from __future__ import annotations


class <HarnessName>Harness:
    """Typed interface to <description>."""

    def __init__(self, <dependency>) -> None:
        self._<dep> = <dependency>

    def <action>(self) -> <ReturnType>:
        """<What this method does.>

        Returns <typed, parsed result> — never raw strings.
        """
        ...
```

### Design rules

1. **Typed returns.** Every method returns a Python type (dataclass, dict,
   list, int, bool). Tests never parse raw strings.

2. **Defensive parsing.** If the output format changes, the harness raises
   `AssertionError` with the raw output — tests get a clear signal.

3. **No test logic.** Harnesses perform actions and parse results. They do
   not assert or make pass/fail decisions. That belongs in the test file.

4. **Session-safe.** Methods must not accumulate unbounded state. If the
   harness collects messages (like MqttHarness), provide a `clear()` method.

---

## Step 2 — Register a fixture in conftest.py

File: `tests/integration/pytest/conftest.py`

Add the import at the top:
```python
from harnesses.<harness_name>_harness import <HarnessName>Harness
```

Add the fixture (session-scoped, because `pytest_dut_scope: session`):
```python
@pytest.fixture(scope="session")
def <harness_name>_harness(<dependencies>):
    """<Description>."""
    harness = <HarnessName>Harness(<args>)
    # If the harness needs setup (connect, subscribe, etc.):
    # harness.connect()
    yield harness
    # Teardown if needed:
    # harness.disconnect()
```

**Dependency injection:**
- Need shell access? → depend on `shell` (from twister_harness)
- Need the DUT booted first? → depend on `dut`
- Need HTTP access? → depend on `http_harness` or `dut`
- Standalone (e.g. external tool)? → no dependencies

---

## Step 3 — Register a marker (if needed)

If the harness represents a new interaction surface that tests might want
to filter on, add a marker in `conftest.py`:

```python
def pytest_configure(config: pytest.Config) -> None:
    # ... existing markers ...
    config.addinivalue_line("markers", "<name>: <description>")
```

---

## Step 4 — Validate

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/integration \
  --inline-logs -v -N

pre-commit run --all-files
```

---

## Existing harnesses for reference

| Harness | File | Wraps |
|---------|------|-------|
| `ShellHarness` | `harnesses/shell_harness.py` | Zephyr shell (fake_sensors, kernel, config_cmd) |
| `HttpHarness` | `harnesses/http_harness.py` | HTTP dashboard API (GET/POST on port 8080) |
| `MqttHarness` | `harnesses/mqtt_harness.py` | MQTT subscriber (paho-mqtt, auto-skip if no broker) |

---

## Common mistakes to avoid

- **Do not** put test assertions in the harness — harnesses return data,
  tests make judgements.
- **Do not** use function-scoped fixtures — the DUT is session-scoped, so
  all harnesses must be session-scoped too. Function-scoped fixtures that
  depend on session-scoped fixtures cause confusing errors.
- **Do not** import `paho-mqtt`, `requests`, or other optional dependencies
  at module level without a try/except guard — the import will break the
  entire suite if the package isn't installed.
- **Do not** forget to add the import and fixture in conftest.py — the harness
  file alone isn't discovered by pytest.
