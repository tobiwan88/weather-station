# SPDX-License-Identifier: Apache-2.0
"""
HTTP dashboard API tests.

Verifies that the HTTP dashboard (lib/http_dashboard, port 8080) responds
correctly to all its endpoints and that sensor data written via the shell is
reflected in the API JSON within a reasonable timeout.

Markers:
  smoke  — reachability check, run first
  http   — tests that interact via HTTP
"""

import time

import pytest



@pytest.mark.smoke
@pytest.mark.http
def test_dashboard_page_reachable(http_harness):
    """``GET /`` must return an HTML page (Chart.js dashboard)."""
    html = http_harness.get_dashboard_page()
    assert len(html) > 0, "Dashboard page returned empty response"


@pytest.mark.smoke
@pytest.mark.http
def test_api_data_endpoint_returns_json(http_harness):
    """``GET /api/data`` must return JSON with a ``sensors`` key."""
    data = http_harness.get_sensor_data()
    assert "sensors" in data, f"Missing 'sensors' key in response: {data}"


@pytest.mark.http
def test_api_data_has_sensor_entries(shell_harness, http_harness):
    """After a manual trigger, ``/api/data`` must contain sensor entries."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=1)
    assert len(data["sensors"]) >= 1, "No sensor entries in /api/data after trigger"


@pytest.mark.http
def test_api_data_sensor_schema(shell_harness, http_harness):
    """Each sensor entry must have the required fields: uid, label, type, readings."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=4)
    for sensor in data["sensors"]:
        assert "uid" in sensor, f"Missing 'uid' in sensor: {sensor}"
        assert "type" in sensor, f"Missing 'type' in sensor: {sensor}"
        assert "readings" in sensor, f"Missing 'readings' in sensor: {sensor}"


@pytest.mark.http
def test_api_data_reading_schema(shell_harness, http_harness):
    """Each reading must have ``t`` (timestamp) and ``v`` (value) fields."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=1)
    for sensor in data["sensors"]:
        for reading in sensor.get("readings", []):
            assert "t" in reading, f"Missing 't' in reading: {reading}"
            assert "v" in reading, f"Missing 'v' in reading: {reading}"


@pytest.mark.http
def test_api_data_has_all_sensor_uids(shell_harness, http_harness):
    """``/api/data`` must list all six sensors from the overlay."""
    shell_harness.trigger_all()
    data = http_harness.wait_for_readings(min_sensors=6)
    uids = {s["uid"] for s in data["sensors"]}
    expected = {0x0001, 0x0002, 0x0003, 0x0004, 0x0011, 0x0012}
    assert expected.issubset(uids), (
        f"Missing UIDs: {expected - uids}. Found: {[hex(u) for u in uids]}"
    )


@pytest.mark.http
def test_api_config_endpoint_returns_json(http_harness):
    """``GET /api/config`` must return a valid JSON object with required fields."""
    config = http_harness.get_config()
    assert isinstance(config, dict), f"Expected dict, got: {type(config)}"
    for field in ("trigger_interval_ms", "sntp_server"):
        assert field in config, f"Missing field '{field}' in /api/config response: {config}"


@pytest.mark.http
def test_api_locations_endpoint_returns_json(http_harness):
    """``GET /api/locations`` must return a valid JSON response."""
    locations = http_harness.get_locations()
    assert locations is not None, "GET /api/locations returned None"


@pytest.mark.http
def test_post_trigger_interval_accepted(authed_harness):
    """``POST /api/config`` with trigger_interval_ms must return 2xx."""
    status = authed_harness.set_trigger_interval(10000)
    assert 200 <= status < 300, f"Unexpected status code: {status}"
    # Restore — pause lets the embedded server close the previous connection
    # and return to its idle poll loop before we open another one.
    time.sleep(0.3)
    authed_harness.set_trigger_interval(5000)
    # Settle after restore before the next test.
    time.sleep(0.3)


@pytest.mark.http
def test_post_config_returns_ok_json(authed_harness):
    """``POST /api/config`` must return JSON body ``{"ok": true}``."""
    body = authed_harness.post_config({"trigger_interval_ms": "5000"})
    assert body == {"ok": True}, f"Unexpected POST /api/config response body: {body}"


# ---------------------------------------------------------------------------
# Auth-specific tests
# ---------------------------------------------------------------------------


@pytest.mark.http
def test_post_config_without_token_returns_401(http_harness):
    """``POST /api/config`` without an Authorization header must return 401."""
    r = http_harness._post("/api/config", {"trigger_interval_ms": "5000"},
                           authenticated=False)
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"


@pytest.mark.http
def test_post_config_with_wrong_token_returns_401(http_harness):
    """``POST /api/config`` with an invalid token must return 401."""
    r = http_harness._post(
        "/api/config",
        {"trigger_interval_ms": "5000"},
        token="deadbeefdeadbeefdeadbeefdeadbeef",
    )
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"


@pytest.mark.http
def test_post_config_with_valid_token_returns_200(authed_harness):
    """``POST /api/config`` with the correct bearer token must succeed."""
    status = authed_harness.set_trigger_interval(5000)
    assert 200 <= status < 300, f"Unexpected status code: {status}"
    time.sleep(0.3)


@pytest.mark.http
def test_get_api_data_open_without_token_by_default(http_harness):
    """``GET /api/data`` must return 200 without auth (read protection off by default)."""
    r = http_harness._get("/api/data", authenticated=False)
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"


@pytest.mark.http
def test_dashboard_and_config_pages_always_open(http_harness):
    """``GET /`` and ``GET /config`` must return 200 regardless of auth config."""
    for path in ("/", "/config"):
        r = http_harness._get(path, authenticated=False)
        assert r.status_code == 200, f"Path {path} returned {r.status_code}"


@pytest.mark.http
def test_token_rotation_invalidates_old_token(authed_harness, shell_harness):
    """After rotation, the old token must be rejected and the new one accepted."""
    old_token = authed_harness._token

    # Rotate via shell and give the command time to complete.
    shell_harness._shell.exec_command("http_dashboard token rotate")
    time.sleep(0.3)

    # Retrieve and store the new token.
    new_token = authed_harness.get_token_from_shell(shell_harness)
    assert new_token != old_token, "Token did not change after rotation"

    # Old token must now be rejected; pace request to avoid connection pool saturation.
    time.sleep(0.3)
    r = authed_harness._post(
        "/api/config",
        {"trigger_interval_ms": "5000"},
        token=old_token,
    )
    assert r.status_code == 401, f"Old token still accepted after rotation: {r.status_code}"

    # Update harness and verify the new token works.
    time.sleep(0.3)
    authed_harness.set_token(new_token)
    status = authed_harness.set_trigger_interval(5000)
    assert 200 <= status < 300, f"New token rejected after rotation: {status}"
    time.sleep(0.3)
