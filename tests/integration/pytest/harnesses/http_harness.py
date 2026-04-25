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
        # The session also automatically stores the HttpOnly session cookie set
        # by POST /api/login, so subsequent requests carry it without extra code.
        self._session = requests.Session()

    def set_token(self, token: str) -> None:
        """Store a bearer token for use in subsequent authenticated requests.

        Used by automation-oriented tests that authenticate via bearer token
        rather than the session cookie login flow.
        """
        self._token = token

    def _auth_headers(self) -> dict:
        """Return Authorization header dict if a bearer token is set."""
        if self._token:
            return {"Authorization": f"Bearer {self._token}"}
        return {}

    # ------------------------------------------------------------------
    # Session cookie auth (browser-style login/logout)
    # ------------------------------------------------------------------

    def login(self, username: str = "admin", password: str = "admin") -> bool:
        """POST /api/login and store the resulting session cookie.

        The ``requests.Session`` in ``self._session`` automatically stores the
        HttpOnly Set-Cookie header returned on success, so all subsequent
        requests via ``_get`` and ``_post`` carry the session cookie.

        Returns True on success (HTTP 200), False otherwise.
        """
        _log.info("login: POST /api/login username=%s", username)
        post_session = requests.Session()
        try:
            r = post_session.post(
                urljoin(self.base_url, "/api/login"),
                data={"username": username, "password": password},
                headers={"Content-Type": "application/x-www-form-urlencoded",
                         "Connection": "close"},
                timeout=self.timeout,
                allow_redirects=False,
            )
        except Exception as exc:
            _log.error("login: connection error: %s", exc)
            return False
        finally:
            time.sleep(0.15)
            post_session.close()

        _log.info("login: status=%d", r.status_code)
        if r.status_code == 200:
            # Transfer the session cookie to the persistent session.
            for c in post_session.cookies:
                self._session.cookies.set(c.name, c.value, domain=c.domain, path=c.path)
            return True
        return False

    def logout(self) -> None:
        """POST /api/logout to invalidate the current session."""
        self._post("/api/logout", {})

    def change_credentials(self, old_user: str, old_pass: str,
                           new_user: str, new_pass: str) -> int:
        """POST /api/change-credentials. Returns the HTTP status code."""
        r = self._post("/api/change-credentials", {
            "old_user": old_user,
            "old_pass": old_pass,
            "new_user": new_user,
            "new_pass": new_pass,
        })
        return r.status_code

    def rotate_api_token(self) -> str:
        """POST /api/token/rotate and return the new token string."""
        r = self._post("/api/token/rotate", {})
        if r.status_code == 200:
            return r.json().get("token", "")
        return ""

    def wait_until_ready(self, timeout: float = 30.0, poll_interval: float = 0.2) -> None:
        """Block until GET /login returns successfully or timeout expires.

        Polls /login (always public, no session required) rather than /
        because / now redirects to /login when unauthenticated.
        Raises ``TimeoutError`` if the server never responds.
        """
        _log.info("waiting for HTTP server at %s (timeout=%ss)", self.base_url, timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                r = self._session.get(
                    urljoin(self.base_url, "/login"),
                    timeout=self.timeout,
                    allow_redirects=False,
                )
                if r.status_code == 200:
                    _log.info("HTTP server ready")
                    return
            except Exception:
                pass
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
        t0 = time.monotonic()
        try:
            r = self._session.get(
                urljoin(self.base_url, path),
                headers=headers,
                timeout=self.timeout,
            )
        except Exception as exc:
            _log.error("GET %s connection error after %.3fs: %s",
                       urljoin(self.base_url, path), time.monotonic() - t0, exc)
            raise
        _log.debug("GET %s → %s (%.3fs)", path, r.status_code, time.monotonic() - t0)
        if r.status_code >= 400:
            _log.debug("GET %s error body: %r", path, r.text[:200])
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

        ``Connection: close`` is sent so the Zephyr HTTP server transitions to
        ``HTTP_SERVER_DONE_STATE`` immediately after responding (rather than
        keeping the slot in keep-alive wait).  This frees the client slot
        within the server thread before the Python side even wakes from the
        pacing sleep, avoiding client-slot exhaustion on back-to-back POSTs.
        The original NSOS EPOLL_CTL_ADD EEXIST race that motivated omitting
        this header is resolved by CONFIG_ZVFS_POLL_MAX=8,
        CONFIG_NET_MAX_CONTEXTS=16, and the NSOS spinlock patches.

        ``token`` overrides ``self._token`` for this single call, allowing
        tests to send a specific (e.g. wrong or rotated) bearer value without
        mutating harness state.
        """
        headers = {"Connection": "close"}
        if token is not None:
            headers["Authorization"] = f"Bearer {token}"
            _log.info("_post: using explicit token=<redacted>")
        elif authenticated:
            auth = self._auth_headers()
            headers.update(auth)
            _log.info("_post: using auth headers, token=%s",
                      "<set>" if auth.get("Authorization") else "NONE")
        else:
            _log.info("_post: no auth")
        # Forward session cookie (set by login()) to the fresh POST connection.
        session_cookie = self._session.cookies.get("session")
        if session_cookie:
            headers["Cookie"] = f"session={session_cookie}"
            _log.info("_post: forwarding session cookie")
        safe_headers = {k: ("<redacted>" if k in ("Authorization", "Cookie") else v)
                        for k, v in headers.items()}
        _log.info("_post: final headers = %s", safe_headers)
        post_session = requests.Session()
        t0 = time.monotonic()
        try:
            r = post_session.post(
                urljoin(self.base_url, path),
                data=data,
                headers=headers,
                timeout=self.timeout,
                allow_redirects=False,
            )
        except Exception as exc:
            _log.error("POST %s connection error after %.3fs: %s",
                       urljoin(self.base_url, path), time.monotonic() - t0, exc)
            raise
        finally:
            # Sleep first, then close: gives the embedded server time to complete
            # any in-flight epoll operations before the TCP FIN arrives.
            time.sleep(0.15)
            post_session.close()
        elapsed = time.monotonic() - t0
        _log.debug("POST %s %s → %s (%.3fs)", path, list(data.keys()), r.status_code, elapsed)
        if r.status_code >= 400:
            _log.debug("POST %s error body: %r", path, r.text[:200])
        return r

    def get_token_from_shell(self, shell_harness) -> str:
        """Read the active bearer token from the Zephyr shell.

        Runs ``http_dashboard token show`` and parses the output line:
          Authorization: Bearer <32hexchars>

        Returns the 32-character hex token string.
        Raises ``AssertionError`` if the output cannot be parsed.
        """
        lines = shell_harness.exec("http_dashboard token show")
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
