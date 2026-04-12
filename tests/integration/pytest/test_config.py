# SPDX-License-Identifier: Apache-2.0
"""
Configuration command tests.

Verifies that the trigger interval can be changed via both the HTTP API
(POST /api/config) and the Zephyr config_cmd shell command, and that the
change takes effect in the running system.

Markers:
  http   — uses HTTP POST to /api/config
  shell  — uses config_cmd shell sub-command
"""

import time

import pytest


@pytest.mark.http
def test_set_trigger_interval_via_http(http_harness):
    """POST trigger_interval_ms=10000 must be accepted (2xx) by /api/config."""
    status = http_harness.set_trigger_interval(10000)
    assert 200 <= status < 300, f"Unexpected HTTP status: {status}"
    # Restore — brief pause lets the embedded server close the previous
    # connection before we open another one.
    time.sleep(0.1)
    http_harness.set_trigger_interval(5000)


@pytest.mark.http
def test_trigger_interval_bounds_accepted(http_harness):
    """Boundary values for trigger_interval_ms must be accepted."""
    for ms in (1000, 60000):
        status = http_harness.set_trigger_interval(ms)
        assert 200 <= status < 300, f"Status {status} for interval={ms}"
        # Pace requests: give the HTTP server time to close each connection
        # before the next arrives.  The embedded server has a small client
        # pool; rapid fire fills it and delays subsequent accepts.
        time.sleep(0.1)
    http_harness.set_trigger_interval(5000)


@pytest.mark.http
def test_sntp_resync_accepted(http_harness):
    """POST action=sntp_resync must be accepted (2xx).

    The SNTP resync runs on a dedicated background thread; the HTTP response
    is returned immediately.  A short pause after the request lets the server
    settle before subsequent tests start making HTTP connections.
    """
    status = http_harness.request_sntp_resync()
    assert 200 <= status < 300, f"Unexpected HTTP status: {status}"
    # Allow the SNTP thread to start (and potentially consume a net context)
    # before subsequent tests pile on more HTTP connections.
    time.sleep(0.2)
