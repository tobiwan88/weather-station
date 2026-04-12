# SPDX-License-Identifier: Apache-2.0
"""
HTTP harness — typed client for the weather-station HTTP dashboard.

Wraps the ``requests`` library and surfaces each endpoint as a method with
a typed return value. Tests never construct URLs or parse raw responses.
"""

from __future__ import annotations

import time
from urllib.parse import urljoin

import requests


class HttpHarness:
    """Typed interface to the HTTP dashboard API (lib/http_dashboard)."""

    def __init__(self, base_url: str = "http://localhost:8080", timeout: float = 5.0) -> None:
        self.base_url = base_url
        self.timeout = timeout

    def wait_until_ready(self, timeout: float = 30.0, poll_interval: float = 0.2) -> None:
        """Block until GET / returns successfully or timeout expires.

        Called by the ``http_harness`` fixture before any test runs so that
        tests do not race against the HTTP server's bind on port 8080.
        Raises ``TimeoutError`` if the server never responds.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self._get("/")
                return
            except Exception:
                time.sleep(poll_interval)
        raise TimeoutError(f"HTTP server at {self.base_url} not reachable within {timeout}s")

    def _get(self, path: str) -> requests.Response:
        return requests.get(
            urljoin(self.base_url, path),
            headers={"Connection": "close"},
            timeout=self.timeout,
        )

    def _post(self, path: str, data: dict) -> requests.Response:
        return requests.post(
            urljoin(self.base_url, path),
            data=data,
            headers={"Connection": "close"},
            timeout=self.timeout,
            allow_redirects=False,
        )

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
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.get_sensor_data()
            sensors_with_readings = [
                s for s in data.get("sensors", []) if s.get("readings")
            ]
            if len(sensors_with_readings) >= min_sensors:
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
