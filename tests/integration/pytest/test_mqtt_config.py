# SPDX-License-Identifier: Apache-2.0
"""
MQTT configuration tests.

Verifies that MQTT broker settings can be changed at runtime via the HTTP
dashboard API and the UART shell, and that enable/disable controls work
correctly.

Markers:
  http   — uses HTTP dashboard API
  shell  — uses Zephyr shell interaction
  mqtt   — requires mosquitto broker
  e2e    — end-to-end data-flow tests
"""

import time

import pytest


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def restore_mqtt_config(authed_harness, shell_harness):
    """Restore MQTT config to defaults after the test suite.

    Restores: host=localhost, port=1883, enabled=true, gateway=weather.
    """
    yield
    time.sleep(0.3)
    try:
        authed_harness.post_config({
            "mqtt_host": "localhost",
            "mqtt_port": "1883",
            "mqtt_enabled": "on",
            "mqtt_gw": "weather",
        })
    except Exception:
        pass
    time.sleep(0.3)


# ---------------------------------------------------------------------------
# HTTP API tests
# ---------------------------------------------------------------------------


@pytest.mark.http
def test_mqtt_config_in_get_api(authed_harness):
    """GET /api/config must include an 'mqtt' object with expected fields."""
    cfg = authed_harness.get_config()
    assert "mqtt" in cfg, "GET /api/config must include 'mqtt' key"

    mqtt = cfg["mqtt"]
    assert "enabled" in mqtt, "mqtt config must have 'enabled'"
    assert "host" in mqtt, "mqtt config must have 'host'"
    assert "port" in mqtt, "mqtt config must have 'port'"
    assert "user" in mqtt, "mqtt config must have 'user'"
    assert "gateway" in mqtt, "mqtt config must have 'gateway'"
    assert "keepalive" in mqtt, "mqtt config must have 'keepalive'"


@pytest.mark.http
def test_mqtt_set_broker_via_http(authed_harness, restore_mqtt_config):
    """POST mqtt_host + mqtt_port must be accepted and reflected in GET."""
    time.sleep(0.3)
    resp = authed_harness.post_config({
        "mqtt_host": "192.168.1.100",
        "mqtt_port": "8883",
    })
    assert isinstance(resp, dict), f"Expected dict response, got {type(resp)}"
    time.sleep(0.5)

    cfg = authed_harness.get_config()
    assert cfg["mqtt"]["host"] == "192.168.1.100", "host not updated"
    assert cfg["mqtt"]["port"] == 8883, "port not updated"


@pytest.mark.http
def test_mqtt_set_gateway_via_http(authed_harness, restore_mqtt_config):
    """POST mqtt_gw must update the gateway name."""
    time.sleep(0.3)
    resp = authed_harness.post_config({
        "mqtt_gw": "test-gateway",
    })
    assert isinstance(resp, dict), f"Expected dict response, got {type(resp)}"
    time.sleep(0.5)

    cfg = authed_harness.get_config()
    assert cfg["mqtt"]["gateway"] == "test-gateway", "gateway not updated"


@pytest.mark.http
def test_mqtt_set_keepalive_via_http(authed_harness, restore_mqtt_config):
    """POST mqtt_keepalive must update the keepalive interval."""
    time.sleep(0.3)
    resp = authed_harness.post_config({
        "mqtt_keepalive": "120",
    })
    assert isinstance(resp, dict), f"Expected dict response, got {type(resp)}"
    time.sleep(0.5)

    cfg = authed_harness.get_config()
    assert cfg["mqtt"]["keepalive"] == 120, "keepalive not updated"


@pytest.mark.http
def test_mqtt_set_auth_via_http(authed_harness, restore_mqtt_config):
    """POST mqtt_user must update the username."""
    time.sleep(0.3)
    resp = authed_harness.post_config({
        "mqtt_user": "mqttuser",
    })
    assert isinstance(resp, dict), f"Expected dict response, got {type(resp)}"
    time.sleep(0.5)

    cfg = authed_harness.get_config()
    assert cfg["mqtt"]["user"] == "mqttuser", "username not updated"


# ---------------------------------------------------------------------------
# Shell tests
# ---------------------------------------------------------------------------


def _lines_contain(lines, text):
    """Check if any line in the output contains the given text (case-insensitive)."""
    return any(text.lower() in line.lower() for line in lines)


@pytest.mark.shell
def test_mqtt_pub_status_shows_config(shell_harness):
    """mqtt_pub status must show broker, gateway, and enabled state."""
    lines = shell_harness.exec("mqtt_pub status")
    assert _lines_contain(lines, "broker"), "status must show broker"
    assert _lines_contain(lines, "gateway"), "status must show gateway"
    assert _lines_contain(lines, "enabled"), "status must show enabled state"


@pytest.mark.shell
def test_mqtt_pub_enable_disable(shell_harness):
    """mqtt_pub enable and mqtt_pub disable must succeed."""
    lines = shell_harness.exec("mqtt_pub disable")
    assert _lines_contain(lines, "disabled"), "disable must confirm"

    lines = shell_harness.exec("mqtt_pub status")
    # Status shows "enabled : no" and "state : disconnected" after disable
    assert _lines_contain(lines, "no") or _lines_contain(lines, "disabled"), \
        f"status must show disabled state: {lines}"

    lines = shell_harness.exec("mqtt_pub enable")
    assert _lines_contain(lines, "enabled"), "enable must confirm"

    lines = shell_harness.exec("mqtt_pub status")
    # After re-enable, state could be "disconnected", "connecting", or "connected"
    # but "enabled" must show "yes"
    assert any("yes" in line.lower() for line in lines if "enabled" in line.lower()), \
        f"status must show enabled=yes after re-enable: {lines}"


@pytest.mark.shell
def test_mqtt_pub_set_server(shell_harness, restore_mqtt_config):
    """mqtt_pub set server <host> <port> must succeed."""
    lines = shell_harness.exec("mqtt_pub set server 10.0.0.1 9999")
    assert _lines_contain(lines, "broker set") or _lines_contain(lines, "reconnect"), \
        f"set server must confirm: {lines}"

    lines = shell_harness.exec("mqtt_pub status")
    assert any("10.0.0.1" in line for line in lines), "status must show new host"
    assert any("9999" in line for line in lines), "status must show new port"

    # Restore
    shell_harness.exec("mqtt_pub set server localhost 1883")


@pytest.mark.shell
def test_mqtt_pub_set_gateway(shell_harness, restore_mqtt_config):
    """mqtt_pub set gateway <name> must succeed."""
    lines = shell_harness.exec("mqtt_pub set gateway shell-test-gw")
    assert _lines_contain(lines, "gateway name set"), "set gateway must confirm"

    lines = shell_harness.exec("mqtt_pub status")
    assert any("shell-test-gw" in line for line in lines), "status must show new gateway name"

    # Restore
    shell_harness.exec("mqtt_pub set gateway weather")


# ---------------------------------------------------------------------------
# E2E tests (require mosquitto)
# ---------------------------------------------------------------------------


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.mqtt
def test_mqtt_disabled_stops_publishing(shell_harness, mqtt_harness):
    """When MQTT is disabled, triggering sensors must NOT publish to broker."""
    mqtt_harness.clear()

    # Disable MQTT
    shell_harness.exec("mqtt_pub disable")
    time.sleep(0.5)

    # Trigger all sensors
    shell_harness.trigger_all()
    time.sleep(2.0)

    # No messages should have been published
    msgs = mqtt_harness.messages()
    assert len(msgs) == 0, "No messages expected when MQTT is disabled"

    # Re-enable for subsequent tests
    shell_harness.exec("mqtt_pub enable")
    time.sleep(1.0)


@pytest.mark.e2e
@pytest.mark.shell
@pytest.mark.mqtt
def test_mqtt_enabled_resumes_publishing(shell_harness, mqtt_harness):
    """After re-enabling MQTT, triggering sensors must publish to broker."""
    # Ensure MQTT is enabled
    shell_harness.exec("mqtt_pub enable")
    time.sleep(1.0)

    mqtt_harness.clear()

    # Trigger all sensors
    shell_harness.trigger_all()
    time.sleep(2.0)

    msgs = mqtt_harness.messages()
    assert len(msgs) > 0, "Messages expected after re-enabling MQTT"
