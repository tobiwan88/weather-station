# HTTP Dashboard

> Design rationale: [decisions](../decisions.md).

`lib/http_dashboard` is a self-contained Zephyr library enabled by `CONFIG_HTTP_DASHBOARD=y`. When disabled it contributes zero code or RAM. The dashboard listens on port 8080 (configurable via `HTTP_DASHBOARD_PORT`) and provides a browser-based interface to live sensor data and runtime configuration.

---

## Endpoints

| Endpoint | Purpose |
|---|---|
| `GET /` | Embedded Chart.js timeseries page; polls `/api/data` every second |
| `GET /config` | Embedded HTML configuration form |
| `GET /login` | Login page (public, no auth required) |
| `GET /api/data` | JSON snapshot of the ring buffer (last N readings per sensor) |
| `GET /api/config` / `POST /api/config` | Read or update runtime config: `trigger_interval_ms`, `sntp_server`, `action=sntp_resync`, sensor metadata, location management, MQTT settings |
| `POST /api/login` | Authenticate with username/password; returns session cookie. Returns 429 + Retry-After after `HTTP_DASHBOARD_AUTH_MAX_FAILS` consecutive failures |
| `POST /api/logout` | Invalidate current session |
| `POST /api/change-credentials` | Update username and password (authenticated) |
| `POST /api/token/rotate` | Rotate the API bearer token (authenticated) |
| `GET /api/locations` | JSON list of registered locations (authenticated) |

`sntp_server` is applied via `net_config_sntp_set_server()`; `action=sntp_resync` starts an upstream async resync. `GET /api/config` does not return the API token.

The split between a human UI (`/`, `/config`) and a data API (`/api/*`) keeps the API stable even if the HTML changes.

---

## Ring-buffer snapshot pattern

zbus listener callbacks run in the publisher's context. When the publisher is a `k_timer` expiry function, the listener runs from an ISR. A mutex cannot be acquired from ISR context; a spinlock is ISR-safe and appropriate because the critical section is a short `memcpy`.

The snapshot pattern keeps the spinlock held only for the copy; JSON serialisation happens outside the lock:

```
acquire spinlock
    memcpy ring buffer → local stack copy
release spinlock

serialize JSON from local copy   ← no lock held
```

See also: [concurrency.md](concurrency.md) and the [Snapshot Pattern diagram](diagrams.md#concurrency--snapshot-pattern).

---

## Linker fragment requirement

Zephyr's HTTP server discovers resource descriptors via a linker-collected iterable section. The library ships `http_dashboard_sections.ld` containing:

```ld
ITERABLE_SECTION_ROM(http_resource_desc_dashboard_svc, 4)
```

This file must be included in the build. Omitting it causes undefined-reference linker errors. The library's `CMakeLists.txt` adds it automatically when `CONFIG_HTTP_DASHBOARD=y` — no manual step is required.

---

## Authentication

When `CONFIG_HTTP_DASHBOARD_AUTH=y`:

- A browser login page (`GET /login`, `POST /api/login`) validates username and password and issues an `HttpOnly` session cookie.
- Automation clients send `Authorization: Bearer <token>`.
- Both mechanisms guard all `/api/*` and `/config` endpoints.
- Credentials and the API token are persisted in Zephyr settings under `dash/user`, `dash/pass`, and `dash/token`.
- First-boot defaults are set via Kconfig (`lib/http_dashboard/Kconfig`).
- `CONFIG_HTTP_DASHBOARD_AUTH_FORCE_PW_CHANGE` (default y) forces a credential change while first-boot defaults are in use.
- `CONFIG_HTTP_DASHBOARD_AUTH_MAX_FAILS` (default 5) sets the consecutive failed logins before throttling.

When `CONFIG_HTTP_DASHBOARD_AUTH=n` all endpoints remain open — suitable for local development without credentials. Integration tests that exercise authenticated endpoints must use the `authed_harness` fixture or supply a bearer token.

---

## Web assets
`src/*.html`, `*.js`, `*.css` are bundled into generated `.inc` headers at build time and served from flash. LittleFS is optional future work.
