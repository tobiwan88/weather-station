# SPDX-License-Identifier: Apache-2.0
"""
System tests: sensor-node → gateway FIFO data flow.

Boots a real sensor-node.exe subprocess alongside the Twister-managed
gateway.exe. The sensor-node writes protobuf SensorReading frames to a
POSIX FIFO; the gateway pipe_transport reads and publishes them to
sensor_event_chan → HTTP /api/data.

Auto-skipped when SENSOR_NODE_EXE env var is not set.

Markers: system, http, shell
"""
import time

import pytest


@pytest.mark.system
@pytest.mark.http
def test_sensor_node_reading_appears_in_gateway(sensor_node_harness, authed_harness):
    """Triggering the sensor-node must produce readings in the gateway /api/data."""
    sensor_node_harness.trigger_all()
    time.sleep(0.5)
    data = authed_harness.wait_for_readings(min_sensors=1, timeout=15.0)
    assert data["sensors"], "No sensors in /api/data after sensor-node trigger"


@pytest.mark.system
@pytest.mark.http
def test_sensor_node_value_reflected_in_gateway(sensor_node_harness, authed_harness):
    """A value set on the sensor-node via shell must appear in the gateway /api/data."""
    sensor_node_harness.set_temp(0x0021, 3000)   # 3.0 °C — sensor-node overlay uid
    sensor_node_harness.trigger_all()
    time.sleep(0.5)
    data = authed_harness.wait_for_readings(min_sensors=1, timeout=15.0)
    by_uid = {s["uid"]: s for s in data["sensors"]}
    assert 0x0021 in by_uid, (
        f"uid 0x0021 not found in gateway /api/data; present: {[hex(u) for u in by_uid]}"
    )
    readings = by_uid[0x0021]["readings"]
    assert readings, "No readings for uid 0x0021"
    latest_v = readings[-1]["v"]
    assert -2.0 <= latest_v <= 8.0, f"Expected ~3.0 °C, got {latest_v}"


@pytest.mark.system
@pytest.mark.shell
def test_sensor_node_uid_registered_in_gateway(sensor_node_harness, shell_harness):
    """After sensor-node trigger the gateway sensor_registry must list the sensor uid."""
    sensor_node_harness.trigger_all()
    time.sleep(0.5)
    lines = shell_harness.exec("sensor_registry list")
    joined = " ".join(lines)
    assert "0021" in joined, f"uid 0x0021 not in gateway sensor_registry list: {joined}"
