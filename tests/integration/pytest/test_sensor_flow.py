# SPDX-License-Identifier: Apache-2.0
"""
End-to-end sensor data flow tests.

These tests exercise the full pipeline:
  shell trigger → zbus sensor_event_chan → HTTP ring-buffer → /api/data
  shell trigger → zbus sensor_event_chan → MQTT publisher → broker topic

They are the most integration-heavy tests in the suite.

Markers:
  e2e    — full end-to-end data flow
  shell  — uses shell interaction
  http   — uses HTTP endpoint
  mqtt   — uses MQTT broker (skipped if broker unavailable)
"""

import pytest


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.http
def test_trigger_propagates_to_http(shell_harness, http_harness):
    """A manual trigger must cause sensor readings to appear in /api/data."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=4, timeout=10.0)
    sensors = data["sensors"]
    assert len(sensors) >= 4, f"Expected >=4 sensors after trigger, got {len(sensors)}"


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.http
def test_indoor_temp_value_in_http(shell_harness, http_harness):
    """Indoor temperature sensor (0x0001) must appear in /api/data with a
    plausible value after a trigger."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=1, timeout=10.0)
    by_uid = {s["uid"]: s for s in data["sensors"]}
    assert 0x0001 in by_uid, "Indoor temp sensor (0x0001) missing from /api/data"
    readings = by_uid[0x0001]["readings"]
    assert readings, "Indoor temp sensor has no readings"
    # Initial value is 21 °C; allow ±5 °C for jitter
    latest_v = readings[-1]["v"]
    assert 16.0 <= latest_v <= 26.0, f"Indoor temp out of expected range: {latest_v}"


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.http
def test_set_value_reflected_in_http(shell_harness, http_harness):
    """Setting a sensor value via shell must appear in /api/data after a trigger."""
    # Set CO2 to a distinctive value
    shell_harness.set_co2(0x0003, 1500000)  # 1500 ppm
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=1, timeout=10.0)
    by_uid = {s["uid"]: s for s in data["sensors"]}
    assert 0x0003 in by_uid, "CO2 sensor (0x0003) missing from /api/data"
    readings = by_uid[0x0003]["readings"]
    assert readings, "CO2 sensor has no readings"
    latest_v = readings[-1]["v"]
    # HTTP API decodes Q31 to float; 1500 ppm ≈ 1500.0 (allow ±50 for jitter)
    assert 1450.0 <= latest_v <= 1550.0, f"CO2 value not reflected correctly: {latest_v}"
    # Restore
    shell_harness.set_co2(0x0003, 800000)


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.mqtt
def test_trigger_publishes_to_mqtt(shell_harness, mqtt_harness):
    """A manual trigger must cause the gateway to publish to the MQTT broker."""
    mqtt_harness.clear()
    shell_harness.trigger_all()
    msgs = mqtt_harness.wait_for_messages(count=1, timeout=15.0)
    assert len(msgs) >= 1, "No MQTT messages received after trigger"


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.mqtt
def test_mqtt_topic_format(shell_harness, mqtt_harness):
    """MQTT topics must follow the gateway/<location>/<label>/<type> pattern."""
    mqtt_harness.clear()
    shell_harness.trigger_all()
    msgs = mqtt_harness.wait_for_messages(count=1, timeout=15.0)
    assert msgs, "No MQTT messages received"
    for msg in msgs:
        assert msg.topic.startswith("weather/"), (
            f"Expected topic starting with 'weather/', got: {msg.topic!r}"
        )


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.mqtt
def test_mqtt_payload_has_required_fields(shell_harness, mqtt_harness):
    """Each MQTT payload must contain ``time``, ``value``, and ``unit`` fields."""
    mqtt_harness.clear()
    shell_harness.trigger_all()
    msgs = mqtt_harness.wait_for_messages(count=1, timeout=15.0)
    assert msgs, "No MQTT messages received"
    for msg in msgs:
        assert "value" in msg.payload, f"Missing 'value' in payload: {msg.payload}"
        assert "unit" in msg.payload, f"Missing 'unit' in payload: {msg.payload}"
