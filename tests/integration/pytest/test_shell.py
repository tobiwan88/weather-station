# SPDX-License-Identifier: Apache-2.0
"""
Shell interaction tests.

These tests verify that the gateway Zephyr shell is operational and that
the weather-station shell sub-commands respond correctly.  They use only
the UART shell surface — no HTTP or MQTT.

Markers:
  smoke  — run these first as a quick sanity gate
  shell  — tests that interact via the Zephyr shell
"""

import pytest


@pytest.mark.smoke
@pytest.mark.shell
def test_help_lists_fake_sensors(shell_harness):
    """``help`` output must list the fake_sensors sub-command."""
    lines = shell_harness.help()
    assert any("fake_sensors" in line for line in lines), (
        f"'fake_sensors' not found in help output:\n" + "\n".join(lines)
    )


@pytest.mark.smoke
@pytest.mark.shell
def test_device_is_alive(shell_harness):
    """Device uptime must be a positive number of milliseconds."""
    ms = shell_harness.get_uptime_ms()
    assert ms > 0, f"Unexpected uptime: {ms} ms"


@pytest.mark.shell
def test_zephyr_version_reported(shell_harness):
    """``kernel version`` must return a Zephyr version string."""
    version = shell_harness.get_version()
    assert "Zephyr version" in version, f"Unexpected version string: {version!r}"


@pytest.mark.shell
def test_sensor_list_has_all_sensors(shell_harness):
    """``fake_sensors list`` must return the six sensors defined in the overlay.

    Expected UIDs: 0x0001 (temp indoor), 0x0002 (hum indoor),
                   0x0003 (CO2), 0x0004 (VOC),
                   0x0011 (temp outdoor), 0x0012 (hum outdoor).
    """
    sensors = shell_harness.list_sensors()
    uids = {s.uid for s in sensors}
    expected = {0x0001, 0x0002, 0x0003, 0x0004, 0x0011, 0x0012}
    assert expected.issubset(uids), (
        f"Missing UIDs: {expected - uids}. Found: {[hex(u) for u in uids]}"
    )


@pytest.mark.shell
def test_sensor_kinds_are_correct(shell_harness):
    """Verify that each sensor UID reports the expected physical kind."""
    sensors = shell_harness.list_sensors()
    by_uid = {s.uid: s for s in sensors}

    assert by_uid[0x0001].kind == "temperature"
    assert by_uid[0x0002].kind == "humidity"
    assert by_uid[0x0003].kind == "co2"
    assert by_uid[0x0004].kind == "voc"
    assert by_uid[0x0011].kind == "temperature"
    assert by_uid[0x0012].kind == "humidity"


@pytest.mark.shell
def test_sensor_initial_values(shell_harness):
    """Sensor initial values must match the DT overlay definitions."""
    sensors = shell_harness.list_sensors()
    by_uid = {s.uid: s for s in sensors}

    assert by_uid[0x0001].value_milli == 21000   # 21 °C
    assert by_uid[0x0002].value_milli == 50000   # 50 %RH
    assert by_uid[0x0003].value_milli == 800000  # 800 ppm CO2
    assert by_uid[0x0004].value_milli == 25000   # 25 IAQ VOC
    assert by_uid[0x0011].value_milli == 4000    # 4 °C outdoor
    assert by_uid[0x0012].value_milli == 30000   # 30 %RH outdoor


@pytest.mark.shell
def test_manual_trigger_fires(shell_harness):
    """``fake_sensors trigger`` must confirm the broadcast trigger was sent."""
    result = shell_harness.trigger_all()
    assert "trigger" in result.lower(), f"Unexpected trigger response: {result!r}"


@pytest.mark.shell
def test_set_temperature_via_shell(shell_harness):
    """Setting temperature via shell must be reflected in the sensor list."""
    shell_harness.set_temperature(0x0001, 25000)  # 25 °C
    sensors = shell_harness.list_sensors()
    by_uid = {s.uid: s for s in sensors}
    assert by_uid[0x0001].value_milli == 25000, (
        f"Expected 25000, got {by_uid[0x0001].value_milli}"
    )
    # Restore original value so subsequent tests are not affected.
    shell_harness.set_temperature(0x0001, 21000)
