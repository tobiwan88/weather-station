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

import logging
import time

import pytest

_log = logging.getLogger("test_http_api")


# ---------------------------------------------------------------------------
# Auth tests: login page, session cookie, bearer token
# ---------------------------------------------------------------------------


@pytest.mark.smoke
@pytest.mark.http
def test_login_page_is_public(http_harness):
    """``GET /login`` must return 200 without any authentication."""
    r = http_harness._get("/login", authenticated=False)
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert len(r.text) > 0, "Login page returned empty body"


@pytest.mark.http
def test_unauthenticated_page_redirects(http_harness):
    """``GET /`` and ``GET /config`` must redirect to /login when unauthenticated."""
    for path in ("/", "/config"):
        r = http_harness._session.get(
            f"http://localhost:8080{path}",
            timeout=http_harness.timeout,
            allow_redirects=False,
        )
        assert r.status_code == 302, f"Path {path}: expected 302, got {r.status_code}"
        location = r.headers.get("location", "")
        assert "/login" in location, f"Path {path}: redirect location is {location!r}"


@pytest.mark.http
def test_unauthenticated_api_returns_401(http_harness):
    """``GET /api/data`` and ``GET /api/config`` must return 401 without auth."""
    for path in ("/api/data", "/api/config"):
        r = http_harness._get(path, authenticated=False)
        assert r.status_code == 401, f"Path {path}: expected 401, got {r.status_code}"


@pytest.mark.http
def test_login_bad_credentials(http_harness):
    """``POST /api/login`` with wrong password must return 401."""
    r = http_harness._post("/api/login", {"username": "admin", "password": "wrongpass"},
                           authenticated=False)
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"


@pytest.mark.http
def test_login_success_sets_cookie(http_harness):
    """``POST /api/login`` with correct credentials must return 200 and set a cookie."""
    ok = http_harness.login("admin", "admin")
    assert ok, "Login with default credentials failed"
    cookie = http_harness._session.cookies.get("session")
    assert cookie, "Session cookie not set after login"
    assert len(cookie) == 32, f"Unexpected session cookie length: {len(cookie)}"


@pytest.mark.http
def test_post_config_without_auth_returns_401(http_harness):
    """``POST /api/config`` without any auth must return 401."""
    time.sleep(1)
    r = http_harness._post("/api/config", {"trigger_interval_ms": "5000"},
                           authenticated=False)
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"


@pytest.mark.http
def test_post_config_with_wrong_bearer_token_returns_401(http_harness):
    """``POST /api/config`` with an invalid bearer token must return 401."""
    time.sleep(1)
    r = http_harness._post(
        "/api/config",
        {"trigger_interval_ms": "5000"},
        token="deadbeefdeadbeefdeadbeefdeadbeef",
    )
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"


@pytest.mark.http
def test_logout_invalidates_session(http_harness):
    """After logout, the session cookie must be rejected by /api/data."""
    ok = http_harness.login("admin", "admin")
    assert ok, "Login failed"

    # Verify session works
    r = http_harness._get("/api/data")
    assert r.status_code == 200, f"Expected 200 before logout, got {r.status_code}"

    # Logout
    time.sleep(0.3)
    http_harness.logout()
    time.sleep(0.3)

    # Session should be gone — cookie is still in self._session but server-side
    # slot is invalidated.
    r = http_harness._get("/api/data")
    assert r.status_code == 401, f"Expected 401 after logout, got {r.status_code}"

    # Clear the cookie so subsequent tests start fresh
    http_harness._session.cookies.clear()


@pytest.mark.http
def test_change_credentials_wrong_old_pass(authed_harness):
    """``POST /api/change-credentials`` with wrong old password must return 403."""
    time.sleep(0.5)
    status = authed_harness.change_credentials("admin", "wrongpass", "admin", "newpass")
    assert status == 403, f"Expected 403, got {status}"


@pytest.mark.http
def test_change_credentials_success(authed_harness):
    """Change credentials and verify login with new credentials works; restore afterwards."""
    time.sleep(0.5)
    # Change admin/admin → admin/newpass
    status = authed_harness.change_credentials("admin", "admin", "admin", "newpass")
    assert status == 200, f"Expected 200 for credential change, got {status}"

    time.sleep(0.3)
    # Old password must now fail
    ok = authed_harness.login("admin", "admin")
    assert not ok, "Login with old password should have failed"

    time.sleep(0.3)
    # New password must work
    ok = authed_harness.login("admin", "newpass")
    assert ok, "Login with new password failed"

    # Restore
    time.sleep(0.3)
    status = authed_harness.change_credentials("admin", "newpass", "admin", "admin")
    assert status == 200, f"Credential restore failed with status {status}"
    authed_harness._session.cookies.clear()


@pytest.mark.http
def test_bearer_token_accesses_api(api_token_harness):
    """A valid API bearer token must grant access to ``GET /api/data``."""
    r = api_token_harness._get("/api/data", authenticated=True)
    assert r.status_code == 200, f"Expected 200 with bearer token, got {r.status_code}"


@pytest.mark.http
def test_api_token_rotate(api_token_harness, shell_harness, device_logger):
    """After rotation, the old token must be rejected and the new one accepted."""
    time.sleep(1)
    old_token = api_token_harness._token

    shell_harness.exec("http_dashboard token rotate")
    time.sleep(1)
    device_logger.drain()

    new_token = api_token_harness.get_token_from_shell(shell_harness)
    assert new_token != old_token, "Token did not change after rotation"

    time.sleep(1)
    r = api_token_harness._post("/api/config", {"trigger_interval_ms": "5000"},
                                token=old_token)
    assert r.status_code == 401, f"Old token still accepted: {r.status_code}"

    time.sleep(3)
    api_token_harness.set_token(new_token)
    status = api_token_harness.set_trigger_interval(5000)
    assert 200 <= status < 300, f"New token rejected: {status}"


# ---------------------------------------------------------------------------
# Data and config endpoint tests (require auth)
# ---------------------------------------------------------------------------


@pytest.mark.smoke
@pytest.mark.http
def test_dashboard_page_reachable(authed_harness):
    """``GET /`` must return an HTML page after login."""
    html = authed_harness.get_dashboard_page()
    assert len(html) > 0, "Dashboard page returned empty response"


@pytest.mark.smoke
@pytest.mark.http
def test_api_data_endpoint_returns_json(authed_harness):
    """``GET /api/data`` must return JSON with a ``sensors`` key."""
    data = authed_harness.get_sensor_data()
    assert "sensors" in data, f"Missing 'sensors' key in response: {data}"


@pytest.mark.http
def test_api_data_has_sensor_entries(shell_harness, authed_harness):
    """After a manual trigger, ``/api/data`` must contain sensor entries."""
    shell_harness.trigger_all()
    data = authed_harness.wait_for_readings(min_sensors=1)
    assert len(data["sensors"]) >= 1, "No sensor entries in /api/data after trigger"


@pytest.mark.http
def test_api_data_sensor_schema(shell_harness, authed_harness):
    """Each sensor entry must have the required fields: uid, label, type, readings."""
    shell_harness.trigger_all()
    data = authed_harness.wait_for_readings(min_sensors=4)
    for sensor in data["sensors"]:
        assert "uid" in sensor, f"Missing 'uid' in sensor: {sensor}"
        assert "type" in sensor, f"Missing 'type' in sensor: {sensor}"
        assert "readings" in sensor, f"Missing 'readings' in sensor: {sensor}"


@pytest.mark.http
def test_api_data_reading_schema(shell_harness, authed_harness):
    """Each reading must have ``t`` (timestamp) and ``v`` (value) fields."""
    shell_harness.trigger_all()
    data = authed_harness.wait_for_readings(min_sensors=1)
    for sensor in data["sensors"]:
        for reading in sensor.get("readings", []):
            assert "t" in reading, f"Missing 't' in reading: {reading}"
            assert "v" in reading, f"Missing 'v' in reading: {reading}"


@pytest.mark.http
def test_api_data_has_all_sensor_uids(shell_harness, authed_harness):
    """``/api/data`` must list all six sensors from the overlay."""
    shell_harness.trigger_all()
    data = authed_harness.wait_for_readings(min_sensors=6)
    uids = {s["uid"] for s in data["sensors"]}
    expected = {0x0001, 0x0002, 0x0003, 0x0004, 0x0011, 0x0012}
    assert expected.issubset(uids), (
        f"Missing UIDs: {expected - uids}. Found: {[hex(u) for u in uids]}"
    )


@pytest.mark.http
def test_api_config_endpoint_returns_json(authed_harness):
    """``GET /api/config`` must return a valid JSON object with required fields."""
    config = authed_harness.get_config()
    assert isinstance(config, dict), f"Expected dict, got: {type(config)}"
    for field in ("trigger_interval_ms", "sntp_server"):
        assert field in config, f"Missing field '{field}' in /api/config response: {config}"


@pytest.mark.http
def test_api_config_includes_api_token(authed_harness):
    """``GET /api/config`` must include the ``api_token`` field (32 hex chars)."""
    config = authed_harness.get_config()
    assert "api_token" in config, f"Missing 'api_token' in /api/config: {config}"
    assert len(config["api_token"]) == 32, (
        f"Unexpected api_token length: {len(config['api_token'])}")


@pytest.mark.http
def test_api_locations_endpoint_returns_json(authed_harness):
    """``GET /api/locations`` must return a valid JSON response."""
    locations = authed_harness.get_locations()
    assert locations is not None, "GET /api/locations returned None"


@pytest.mark.http
def test_post_trigger_interval_accepted(authed_harness):
    """``POST /api/config`` with trigger_interval_ms must return 2xx."""
    status = authed_harness.set_trigger_interval(10000)
    assert 200 <= status < 300, f"Unexpected status code: {status}"
    time.sleep(0.3)
    authed_harness.set_trigger_interval(5000)
    time.sleep(0.3)


@pytest.mark.http
def test_post_config_returns_ok_json(authed_harness):
    """``POST /api/config`` must return JSON body ``{"ok": true}``."""
    body = authed_harness.post_config({"trigger_interval_ms": "5000"})
    assert body == {"ok": True}, f"Unexpected POST /api/config response body: {body}"


@pytest.mark.http
def test_two_back_to_back_posts(authed_harness, device_logger):
    """Two consecutive POSTs on the same session must both succeed."""
    time.sleep(1)
    device_logger.drain()
    r1 = authed_harness._post("/api/config", {"trigger_interval_ms": "3000"})
    _log.info("First POST: status=%d", r1.status_code)
    device_logger.drain()
    assert 200 <= r1.status_code < 300, f"First POST failed: {r1.status_code}"

    time.sleep(2)
    device_logger.drain()
    r2 = authed_harness._post("/api/config", {"trigger_interval_ms": "4000"})
    _log.info("Second POST: status=%d", r2.status_code)
    device_logger.drain()
    assert 200 <= r2.status_code < 300, f"Second POST failed: {r2.status_code}"
    time.sleep(1)
