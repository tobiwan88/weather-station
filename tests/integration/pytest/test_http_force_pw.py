# SPDX-License-Identifier: Apache-2.0
"""
First-boot forced credential change and login throttling.

Built with CONFIG_HTTP_DASHBOARD_AUTH_FORCE_PW_CHANGE=y (scenario
weather_station.integration.force_pw).  Each test gets a fresh DUT.
"""

import logging
import time

import pytest

from harnesses.http_harness import HttpHarness

_log = logging.getLogger("test_http_force_pw")

FORCE_PW_PORT = 8081  # CONFIG_HTTP_DASHBOARD_PORT in the force_pw scenario
MAX_FAILS = 5  # CONFIG_HTTP_DASHBOARD_AUTH_MAX_FAILS default


@pytest.fixture()
def http_harness(device_ready):
    """Harness on this scenario's own port (parallel twister scenario uses 8080)."""
    harness = HttpHarness(base_url=f"http://localhost:{FORCE_PW_PORT}")
    harness.wait_until_ready()
    return harness


@pytest.mark.http
def test_default_login_works_but_api_is_blocked(http_harness):
    """Default admin/admin logs in, but /api/data is 403 until creds change."""
    assert http_harness.login("admin", "admin"), "default login must succeed"
    r = http_harness._get("/api/data", authenticated=True)
    assert r.status_code == 403, f"Expected 403, got {r.status_code}"
    assert r.json().get("error") == "password_change_required"


@pytest.mark.http
def test_change_credentials_unblocks_api(http_harness):
    """change-credentials stays reachable and lifts the 403."""
    assert http_harness.login("admin", "admin")
    time.sleep(0.4)
    status = http_harness.change_credentials("admin", "admin", "admin2", "s3cret-pw")
    assert status == 200, f"change-credentials returned {status}"
    time.sleep(0.4)
    r = http_harness._get("/api/data", authenticated=True)
    assert r.status_code == 200, f"Expected 200 after change, got {r.status_code}"


@pytest.mark.http
def test_login_lockout_returns_429_with_retry_after(http_harness):
    """After MAX_FAILS bad logins the next attempt gets 429 + Retry-After."""
    for i in range(MAX_FAILS):
        r = http_harness._post("/api/login",
                               {"username": "admin", "password": f"wrong{i}"},
                               authenticated=False)
        assert r.status_code == 401, f"attempt {i}: expected 401, got {r.status_code}"
        time.sleep(0.4)

    r = http_harness._post("/api/login", {"username": "admin", "password": "admin"},
                           authenticated=False)
    assert r.status_code == 429, f"Expected 429, got {r.status_code}"
    retry = int(r.headers["Retry-After"])
    assert 1 <= retry <= 5, f"unexpected Retry-After {retry}"


@pytest.mark.http
def test_change_to_default_credentials_rejected(http_harness):
    """Re-submitting the first-boot defaults must not lift the 403 gate."""
    assert http_harness.login("admin", "admin")
    time.sleep(0.4)
    status = http_harness.change_credentials("admin", "admin", "admin", "admin")
    assert status == 400, f"Expected 400, got {status}"
    time.sleep(0.4)
    r = http_harness._get("/api/data", authenticated=True)
    assert r.status_code == 403, f"Expected 403, got {r.status_code}"


@pytest.mark.http
def test_bearer_token_is_gated_too(http_harness, shell_harness):
    """The API bearer token is also blocked while credentials are unchanged."""
    http_harness.set_token(http_harness.get_token_from_shell(shell_harness))
    r = http_harness._get("/api/data", authenticated=True)
    assert r.status_code == 403, f"Expected 403, got {r.status_code}"
    assert r.json().get("error") == "password_change_required"
