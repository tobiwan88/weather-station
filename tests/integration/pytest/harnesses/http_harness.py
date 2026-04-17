# SPDX-License-Identifier: Apache-2.0
"""
HTTP harness — typed client for the weather-station HTTP dashboard.

Wraps the ``requests`` library and surfaces each endpoint as a method with
a typed return value. Tests never construct URLs or parse raw responses.
"""

from __future__ import annotations

import logging
import re
import time
from urllib.parse import urljoin

import requests

_log = logging.getLogger("http")


class HttpHarness:
    """Typed interface to the HTTP dashboard API (lib/http_dashboard)."""

    def __init__(self, base_url: str = "http://localhost:8080", timeout: float = 5.0) -> None:
        self.base_url = base_url
        self.timeout = timeout
        self._token: str | None = None
        # Persistent session reuses the same TCP connection across GET requests
        # (HTTP/1.1 keep-alive).  Reusing the connection avoids rapid fd open/
        # close cycles that would race with the Zephyr HTTP server's NSOS epoll
        # cleanup, causing EPOLL_CTL_ADD EEXIST on fd reuse → DUT exit.
        self._session = requests.Session()

    def set_token(self, token: str) -> None:
        """Store the bearer token used for all subsequent authenticated requests."""
        self._token = token

    def _auth_headers(self) -> dict:
        """Return Authorization header dict if a token is set, otherwise empty dict."""
        if self._token:
            return {"Authorization": f"Bearer {self._token}"}
        return {}

    def wait_until_ready(self, timeout: float = 30.0, poll_interval: float = 0.2) -> None:
        """Block until GET / returns successfully or timeout expires.

        Called by the ``http_harness`` fixture before any test runs so that
        tests do not race against the HTTP server's bind on port 8080.
        Raises ``TimeoutError`` if the server never responds.
        """
        _log.info("waiting for HTTP server at %s (timeout=%ss)", self.base_url, timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self._get("/")
                _log.info("HTTP server ready")
                return
            except Exception:
                time.sleep(poll_interval)
        raise TimeoutError(f"HTTP server at {self.base_url} not reachable within {timeout}s")

    def _get(self, path: str, authenticated: bool = False) -> requests.Response:
        """GET request using a persistent keep-alive session.

        A persistent session reuses the same TCP connection across calls,
        eliminating rapid fd open/close cycles that would race with the
        HTTP server's NSOS epoll management on native_sim.

        No ``Connection: close`` header — Zephyr's HTTP server resets the
        connection when that header is sent alongside
        ``HTTP_SERVER_CAPTURE_HEADERS`` (auto-selected by auth).
        """
        headers = self._auth_headers() if authenticated else {}
        r = self._session.get(
            urljoin(self.base_url, path),
            headers=headers,
            timeout=self.timeout,
        )
        _log.debug("GET %s → %s", path, r.status_code)
        return r

    def _post(self, path: str, data: dict, authenticated: bool = True,
              token: str | None = None) -> requests.Response:
        """POST request on a fresh TCP connection.

        Each POST opens a new connection so the Zephyr HTTP server runs its
        ``handle_http_preface`` path, which resets the header-capture context
        (``count``, ``cursor``, ``status``).  On keep-alive connections the
        context is *not* reset between requests (only ``store_next_value`` is),
        so ``header_count`` stays 0 and the Authorization header is never seen
        by the handler even when the client sends it.

        ``Connection: close`` is intentionally omitted — it makes the server
        close the connection immediately after responding, which races with the
        NSOS shared epoll fd on native_sim (EPOLL_CTL_ADD EEXIST, errno=17).
        Instead, the pacing sleep runs while the connection is still open; the
        server-side keepalive timer and the client-side session.close() that
        follows give the embedded server enough time to finish its epoll
        housekeeping before the fd is freed and potentially reused.

        ``token`` overrides ``self._token`` for this single call, allowing
        tests to send a specific (e.g. wrong or rotated) bearer value without
        mutating harness state.
        """
        headers = {}
        if token is not None:
            headers["Authorization"] = f"Bearer {token}"
        elif authenticated:
            headers.update(self._auth_headers())
        _log.debug("POST %s data=%s auth_header_present=%s token=%r",
                   path, list(data.keys()),
                   "Authorization" in headers,
                   token if token is not None else self._token)
        post_session = requests.Session()
        r = post_session.post(
            urljoin(self.base_url, path),
            data=data,
            headers=headers,
            timeout=self.timeout,
            allow_redirects=False,
        )
        _log.debug("POST %s %s → %s", path, list(data.keys()), r.status_code)
        # Sleep first, then close: gives the embedded server time to complete
        # any in-flight epoll operations before the TCP FIN arrives.
        time.sleep(0.15)
        post_session.close()
        return r

    def get_token_from_shell(self, shell_harness) -> str:
        """Read the active bearer token from the Zephyr shell.

        Runs ``http_dashboard token show`` and parses the output line:
          Authorization: Bearer <32hexchars>

        Returns the 32-character hex token string.
        Raises ``AssertionError`` if the output cannot be parsed.
        """
        lines = shell_harness._exec("http_dashboard token show")
        for line in lines:
            m = re.search(r"Bearer\s+([0-9a-fA-F]{32})", line)
            if m:
                return m.group(1)
        raise AssertionError(f"Could not parse bearer token from shell output: {lines}")

    # ------------------------------------------------------------------
    # GET /api/data
    # ------------------------------------------------------------------

    def get_sensor_data(self) -> dict:
        """Return parsed JSON from ``GET /api/data``.

        Shape: ``{"sensors": [{"uid": int, "label": str, "type": str,
                               "readings": [{"t": int, "v": float}]}]}``
        """
        r = self._get("/api/data")
        r.raise_for_status()
        return r.json()

    def wait_for_readings(
        self, min_sensors: int = 1, timeout: float = 15.0, poll_interval: float = 0.5
    ) -> dict:
        """Poll ``/api/data`` until at least ``min_sensors`` have readings.

        Raises ``TimeoutError`` if data does not appear within ``timeout`` seconds.
        """
        _log.info("waiting for readings from %d sensor(s) (timeout=%ss)", min_sensors, timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.get_sensor_data()
            sensors_with_readings = [
                s for s in data.get("sensors", []) if s.get("readings")
            ]
            if len(sensors_with_readings) >= min_sensors:
                _log.info("got readings from %d sensor(s)", len(sensors_with_readings))
                return data
            time.sleep(poll_interval)
        raise TimeoutError(
            f"Expected {min_sensors} sensors with readings within {timeout}s"
        )

    # ------------------------------------------------------------------
    # GET /api/config
    # ------------------------------------------------------------------

    def get_config(self) -> dict:
        """Return parsed JSON from ``GET /api/config``."""
        r = self._get("/api/config")
        r.raise_for_status()
        return r.json()

    # ------------------------------------------------------------------
    # POST /api/config
    # ------------------------------------------------------------------

    def set_trigger_interval(self, ms: int) -> int:
        """POST trigger_interval_ms to ``/api/config``. Returns HTTP status code."""
        r = self._post("/api/config", {"trigger_interval_ms": str(ms)})
        return r.status_code

    def post_config(self, data: dict) -> dict:
        """POST form data to ``/api/config``. Returns parsed JSON response body."""
        r = self._post("/api/config", data)
        r.raise_for_status()
        return r.json()

    def request_sntp_resync(self) -> int:
        """POST sntp_resync action to ``/api/config``. Returns HTTP status code."""
        r = self._post("/api/config", {"action": "sntp_resync"})
        return r.status_code

    # ------------------------------------------------------------------
    # GET /api/locations
    # ------------------------------------------------------------------

    def get_locations(self) -> dict:
        """Return parsed JSON from ``GET /api/locations``."""
        r = self._get("/api/locations")
        r.raise_for_status()
        return r.json()

    # ------------------------------------------------------------------
    # GET / (dashboard page)
    # ------------------------------------------------------------------

    def get_dashboard_page(self) -> str:
        """Return raw HTML from ``GET /``. Useful for smoke-checking reachability."""
        r = self._get("/")
        r.raise_for_status()
        return r.text
