# HTTP Dashboard

> Design rationale: [ADR-011](../adr/ADR-011-http-dashboard.md).

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
| `POST /api/login` | Authenticate with username/password; returns session cookie |
| `POST /api/logout` | Invalidate current session |
| `POST /api/change-credentials` | Update username and password (authenticated) |
| `POST /api/token/rotate` | Rotate the API bearer token (authenticated) |
| `GET /api/locations` | JSON list of registered locations (authenticated) |

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

When `CONFIG_HTTP_DASHBOARD_AUTH=n` all endpoints remain open — suitable for local development without credentials. Integration tests that exercise authenticated endpoints must use the `authed_harness` fixture or supply a bearer token.

---

## HTML/JS embedded as C strings

The Chart.js page and config form are stored as C string literals inside `http_dashboard.c`. This works with zero filesystem dependency on `native_sim` and requires no build-time asset pipeline. It is a known prototype limitation — editing HTML inside a C string is error-prone and prevents live-reload workflows.

The intended target architecture is to serve web assets from LittleFS:

1. Web assets (HTML, CSS, JS) live in a dedicated source directory.
2. At build time they are compiled into a LittleFS filesystem image.
3. The C layer becomes an HTTP router only — no HTML in `.c` files.
4. Live-reload becomes possible by re-flashing only the filesystem partition.

This work is deferred to a future iteration (see backlog: `[HTTP-DASHBOARD] Decouple HTML/JS from C source via LittleFS`).
