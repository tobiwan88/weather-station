# SPDX-License-Identifier: Apache-2.0
"""
SEN0460 PM2.5 sensor integration tests.

These tests run only when CONFIG_SEN0460_SENSOR=y is enabled.
On native_sim the SEN0460 device is not present (no I2C bus), so the driver
gracefully degrades: init logs a warning and the sensor stays inactive.

Markers:
  smoke  — quick sanity checks
  shell  — uses shell interaction
"""

import pytest


@pytest.mark.smoke
@pytest.mark.shell
def test_sen0460_enabled_in_build(device_logger):
    """Verify SEN0460 is compiled into the build.

    Skipped if CONFIG_SEN0460_SENSOR is not enabled.
    """
    entries = device_logger.drain()
    sen0460_msgs = [e for e in entries if "sen0460" in e.module.lower()]
    if not sen0460_msgs:
        pytest.skip("CONFIG_SEN0460_SENSOR not enabled in this build")


@pytest.mark.shell
def test_sen0460_graceful_degrade(shell_harness, device_logger):
    """On native_sim (no I2C), SEN0460 must gracefully degrade.

    The wrapper logs a 'device not ready' warning and skips registry registration.
    """
    entries = device_logger.drain()
    sen0460_msgs = [e for e in entries if "sen0460" in e.module.lower()]
    if not sen0460_msgs:
        pytest.skip("CONFIG_SEN0460_SENSOR not enabled in this build")

    not_ready = [
        e for e in sen0460_msgs
        if "not ready" in e.message.lower() or "device not ready" in e.message.lower()
    ]
    assert not_ready, (
        f"Expected 'device not ready' warning, got: {[e.message for e in sen0460_msgs]}"
    )


@pytest.mark.shell
def test_sen0460_not_in_sensor_registry(shell_harness, device_logger):
    """On native_sim (no I2C), SEN0460 must NOT appear in the sensor registry."""
    entries = device_logger.drain()
    sen0460_msgs = [e for e in entries if "sen0460" in e.module.lower()]
    if not sen0460_msgs:
        pytest.skip("CONFIG_SEN0460_SENSOR not enabled in this build")

    lines = shell_harness.exec("sensor_registry list")
    combined = " ".join(lines)
    assert "sen0460" not in combined.lower(), (
        "SEN0460 should not be in registry on native_sim (no I2C device)"
    )
