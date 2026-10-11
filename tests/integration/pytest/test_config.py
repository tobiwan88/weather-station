# SPDX-License-Identifier: Apache-2.0
"""
Configuration command tests.

Verifies that configuration changes exposed by the HTTP API
(POST /api/config) are accepted and take effect in the running system.

Markers:
  http   — uses HTTP POST to /api/config
"""

import time

import pytest


@pytest.fixture()
def restore_trigger_interval(authed_harness):
    """Restore trigger_interval_ms to the suite default (5000 ms) after the test.

    The leading sleep gives the embedded HTTP server time to close the previous
    connection and return to its idle poll loop before the restore POST arrives.
    The trailing sleep lets the server settle before the next test opens new
    connections (the SNTP resync test follows and opens a background UDP socket).
    """
    yield
    time.sleep(0.3)
    authed_harness.set_trigger_interval(5000)
    time.sleep(0.3)


@pytest.mark.http
def test_set_trigger_interval_via_http(authed_harness, restore_trigger_interval):
    """POST trigger_interval_ms=10000 must be accepted (2xx) by /api/config."""
    status = authed_harness.set_trigger_interval(10000)
    assert 200 <= status < 300, f"Unexpected HTTP status: {status}"


@pytest.mark.http
def test_trigger_interval_bounds_accepted(authed_harness, restore_trigger_interval):
    """Boundary values for trigger_interval_ms must be accepted."""
    time.sleep(1)
    for ms in (1000, 60000):
        status = authed_harness.set_trigger_interval(ms)
        assert 200 <= status < 300, f"Status {status} for interval={ms}"
        # Pace requests: give the embedded server time to close each connection
        # before the next arrives.
        time.sleep(0.5)


@pytest.mark.http
def test_sntp_resync_accepted(authed_harness):
    """POST action=sntp_resync must be accepted (2xx).

    The resync is an async SNTP query on the net socket service (no thread,
    no presync delay); the HTTP response is returned immediately.  The query
    is bounded by CONFIG_NET_CONFIG_SNTP_INIT_TIMEOUT (1000 ms), so subsequent
    tests must not start until that timeout has elapsed.
    """
    status = authed_harness.request_sntp_resync()
    assert 200 <= status < 300, f"Unexpected HTTP status: {status}"
    # Worst-case query timeout (1000 ms) + margin, so the UDP socket is closed
    # before the next test opens more HTTP connections.
    time.sleep(1.5)
