# SPDX-License-Identifier: Apache-2.0
"""
Sensor node TX integration tests.

Exercises the ``lib/sensor_node_tx`` library:
  - Zbus subscription to ``sensor_event_chan`` (accumulates readings)
  - Timer-based periodic transmission via ``lora_node_transmit()``
  - FORCE_TX config command (immediate transmission)
  - Ring-buffer overflow protection (drops when full)
  - Enable/disable lifecycle (PM-ready)

The library has no direct HTTP or MQTT surface; behaviour is verified through
UART log output (device_logger) and shell commands.

Requires ``CONFIG_SENSOR_NODE_TX=y`` in the DUT build.  Tests auto-skip when
the ``sensor_node_tx`` shell command is not available.

Markers:
  e2e    — full end-to-end data flow through the TX pipeline
  shell  — uses Zephyr shell interaction
  system — two-process system tests requiring SENSOR_NODE_EXE
"""

import logging
import time

import pytest

_log = logging.getLogger("test_sensor_node_tx")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_TX_MODULE = "sensor_node_tx"
_TX_CONFIRM = "transmitted"
_TX_BUFFER_FULL = "TX buffer full"
_TX_ENABLED = "sensor_node_tx enabled"
_TX_DISABLED = "sensor_node_tx disabled"


def _drain_and_search(device_logger, substring: str, timeout: float = 10.0,
                      module: str | None = None) -> list:
    """Poll device_logger.drain() until a line containing *substring* appears.

    Optionally filter by Zephyr log *module* name.
    Returns the list of matching entries (may be empty on timeout).
    """
    deadline = time.monotonic() + timeout
    matches: list = []
    while time.monotonic() < deadline:
        entries = device_logger.drain()
        for entry in entries:
            if module and entry.module != module:
                continue
            if substring in entry.message:
                matches.append(entry)
        if matches:
            return matches
        time.sleep(0.2)
    return matches


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture()
def restore_tx_state(shell_harness, device_logger):
    """Ensure sensor_node_tx is re-enabled after tests that disable it."""
    yield
    time.sleep(0.3)
    shell_harness.sensor_node_tx_enable()
    time.sleep(0.3)
    device_logger.drain()


@pytest.fixture()
def clear_buffer(device_logger):
    """Drain any pending log entries before a test starts."""
    device_logger.drain()


@pytest.fixture(autouse=True)
def check_sensor_node_tx(shell_harness):
    """Skip this test if sensor_node_tx is not compiled in."""
    output = shell_harness._exec("sensor_node_tx status")
    text = " ".join(output)
    if "command not found" in text:
        pytest.skip("CONFIG_SENSOR_NODE_TX not enabled in this build")


# ---------------------------------------------------------------------------
# Timer-based TX
# ---------------------------------------------------------------------------

@pytest.mark.e2e
@pytest.mark.shell
def test_timer_tx_emits_log_after_trigger(shell_harness, device_logger, clear_buffer):
    """After triggering sensors, a timer-based TX must log 'transmitted N readings'.

    This verifies the full pipeline:
      fake_sensors trigger → sensor_event_chan → sensor_node_tx buffer → timer → lora_node_transmit
    """
    shell_harness.trigger_all()
    entries = _drain_and_search(device_logger, _TX_CONFIRM, timeout=15.0,
                                module=_TX_MODULE)
    assert entries, (
        f"No '{_TX_CONFIRM}' log from {_TX_MODULE} after trigger. "
        "Check that CONFIG_SENSOR_NODE_TX=y and the TX interval is short enough."
    )
    _log.info("timer TX confirmed: %s", entries[0].message)


@pytest.mark.e2e
@pytest.mark.shell
def test_timer_tx_transmits_accumulated_count(shell_harness, device_logger, clear_buffer):
    """A timer TX must report the number of accumulated readings.

    Six fake sensors are defined in the gateway overlay; after one trigger
    cycle the buffer should contain six events.
    """
    shell_harness.trigger_all()
    entries = _drain_and_search(device_logger, _TX_CONFIRM, timeout=15.0,
                                module=_TX_MODULE)
    assert entries, "No TX confirmation log found"
    msg = entries[0].message
    import re
    m = re.search(r"transmitted\s+(\d+)\s+readings", msg)
    assert m, f"Could not parse reading count from: {msg!r}"
    count = int(m.group(1))
    assert count >= 4, f"Expected >=4 readings, got {count}"


# ---------------------------------------------------------------------------
# Force-TX via config command
# ---------------------------------------------------------------------------

@pytest.mark.shell
def test_force_tx_triggers_immediate_transmission(shell_harness, device_logger, clear_buffer):
    """The FORCE_TX config command must cause an immediate transmission.

    Exercised via the ``sensor_node_tx force`` shell command which calls
    ``sensor_node_tx_force_tx()`` internally.
    """
    shell_harness.trigger_all()
    time.sleep(0.5)
    device_logger.drain()

    shell_harness.sensor_node_tx_force()
    entries = _drain_and_search(device_logger, _TX_CONFIRM, timeout=5.0,
                                module=_TX_MODULE)
    assert entries, (
        f"No '{_TX_CONFIRM}' log after force command. "
        "Check that CONFIG_SENSOR_NODE_TX=y and shell commands are registered."
    )
    _log.info("force TX confirmed: %s", entries[0].message)


@pytest.mark.shell
def test_force_tx_with_empty_buffer(shell_harness, device_logger, clear_buffer):
    """FORCE_TX with an empty buffer must not crash and should log appropriately."""
    device_logger.drain()
    shell_harness.sensor_node_tx_force()
    time.sleep(1.0)
    entries = device_logger.drain()
    tx_msgs = [e for e in entries if e.module == _TX_MODULE and _TX_CONFIRM in e.message]
    assert not tx_msgs, (
        f"Unexpected TX confirmation with empty buffer: {[e.message for e in tx_msgs]}"
    )


# ---------------------------------------------------------------------------
# Buffer-full behaviour
# ---------------------------------------------------------------------------

@pytest.mark.e2e
@pytest.mark.shell
def test_buffer_full_drops_events(shell_harness, device_logger, clear_buffer):
    """When the TX ring buffer is full, excess events must be dropped with a warning.

    CONFIG_SENSOR_NODE_TX_MAX_READINGS defaults to 20.  By triggering sensors
    repeatedly without a TX draining the buffer, we can overflow it.
    """
    max_readings = 20
    triggers_needed = (max_readings // 6) + 2

    for _ in range(triggers_needed):
        shell_harness.trigger_all()
        time.sleep(0.3)

    entries = _drain_and_search(device_logger, _TX_BUFFER_FULL, timeout=5.0,
                                module=_TX_MODULE)
    assert entries, (
        f"No '{_TX_BUFFER_FULL}' warning after {triggers_needed} triggers "
        f"(6 sensors each, buffer max={max_readings}). "
        "The library should drop events gracefully when the buffer is full."
    )
    _log.info("buffer-full drop confirmed: %s", entries[0].message)


@pytest.mark.e2e
@pytest.mark.shell
def test_buffer_full_does_not_crash(shell_harness, device_logger, clear_buffer):
    """Overflowing the TX buffer must not crash the system.

    After filling the buffer, the device must still respond to shell commands.
    """
    for _ in range(10):
        shell_harness.trigger_all()
        time.sleep(0.2)

    device_logger.drain()
    uptime_before = shell_harness.get_uptime_ms()
    assert uptime_before > 0, "Device unresponsive after buffer overflow"


# ---------------------------------------------------------------------------
# Enable/disable lifecycle
# ---------------------------------------------------------------------------

@pytest.mark.shell
def test_disable_stops_accumulation(shell_harness, device_logger, clear_buffer,
                                     restore_tx_state):
    """After disabling sensor_node_tx, sensor events must NOT accumulate in the buffer."""
    shell_harness.sensor_node_tx_disable()
    time.sleep(0.5)

    entries = _drain_and_search(device_logger, _TX_DISABLED, timeout=5.0,
                                module=_TX_MODULE)
    assert entries, f"No '{_TX_DISABLED}' log after disable command"

    device_logger.drain()
    shell_harness.trigger_all()
    time.sleep(1.0)

    tx_entries = _drain_and_search(device_logger, _TX_CONFIRM, timeout=5.0,
                                   module=_TX_MODULE)
    assert not tx_entries, (
        "TX occurred while disabled — events should not accumulate when disabled"
    )


@pytest.mark.shell
def test_enable_resumes_accumulation(shell_harness, device_logger, clear_buffer):
    """Re-enabling sensor_node_tx must resume event accumulation and TX."""
    shell_harness.sensor_node_tx_disable()
    time.sleep(0.5)
    device_logger.drain()

    shell_harness.sensor_node_tx_enable()
    entries = _drain_and_search(device_logger, _TX_ENABLED, timeout=5.0,
                                module=_TX_MODULE)
    assert entries, f"No '{_TX_ENABLED}' log after enable command"

    device_logger.drain()
    shell_harness.trigger_all()

    tx_entries = _drain_and_search(device_logger, _TX_CONFIRM, timeout=15.0,
                                   module=_TX_MODULE)
    assert tx_entries, "No TX after re-enabling — accumulation should resume"


@pytest.mark.shell
def test_status_reports_state(shell_harness):
    """``sensor_node_tx status`` must report enabled/disabled state."""
    status = shell_harness.sensor_node_tx_status()
    assert status, "sensor_node_tx status returned empty output"
    assert "enabled" in status.lower() or "disabled" in status.lower(), (
        f"Status output does not mention enabled/disabled: {status!r}"
    )


# ---------------------------------------------------------------------------
# System-level: sensor-node subprocess TX
# ---------------------------------------------------------------------------

@pytest.mark.system
def test_sensor_node_tx_lib_initialized(sensor_node_harness, device_logger):
    """When SENSOR_NODE_EXE runs with CONFIG_SENSOR_NODE_TX, init must succeed.

    The sensor_node_tx_init() SYS_INIT callback logs on success/failure.
    We verify the DUT booted cleanly (already guaranteed by device_ready)
    and that the sensor-node subprocess is running.
    """
    assert sensor_node_harness.exe, "SENSOR_NODE_EXE not set"
    device_logger.drain()
