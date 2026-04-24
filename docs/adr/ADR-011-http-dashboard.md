# ADR-011 — HTTP Dashboard Web Interface

- **Status:** Accepted
- **Date:** 2026-03-11

---

## Context

During development on `native_sim`, the main monitoring and control surfaces are:

- **Serial shell** — useful for ad-hoc inspection but requires a terminal, knowledge of shell commands, and produces no visualisation.
- **LVGL display** — shows live data but offers no configuration capability and requires physical presence at the machine running the simulator.

Both surfaces fall short for iterative sensor tuning: adjusting the fake-sensor publish interval, changing the SNTP server, or triggering an immediate resync all require shell commands that are easy to forget. A browser-based surface accessible from any device on the local network would remove this friction without requiring any app install or pairing step.

The project already follows a `native_sim`-first approach (ADR-009), so the solution must work on the host without hardware peripherals. Zephyr's built-in HTTP server (`CONFIG_HTTP_SERVER=y`) with iterable-section resource descriptors is a natural fit: it requires no external process, is opt-in via Kconfig, and compiles cleanly under `native_sim/native/64`.

---

## Decision

### 1. HTTP on port 8080, Kconfig-opt-in

Add `lib/http_dashboard` as a self-contained Zephyr library enabled by `CONFIG_HTTP_DASHBOARD=y`. The listening port is `HTTP_DASHBOARD_PORT` (default 8080). When `CONFIG_HTTP_DASHBOARD=n` the library contributes zero code or RAM.

HTTP was chosen over the alternatives listed below because it requires no client install, works from any browser on the LAN, and Zephyr's HTTP server is already present in the tree.

### 2. Four endpoints

| Endpoint | Purpose |
|---|---|
| `GET /` | Embedded Chart.js timeseries page; polls `/api/data` every second |
| `GET /config` | Embedded HTML configuration form |
| `GET /api/data` | JSON snapshot of the ring buffer (last N readings per sensor) |
| `GET /api/config` / `POST /api/config` | Read or update runtime config: `trigger_interval_ms`, `sntp_server`, `action=sntp_resync` |

The split between a human UI (`/`, `/config`) and a data API (`/api/*`) keeps the API stable even if the HTML changes.

### 3. Self-init via SYS_INIT at APPLICATION priority 97

The library registers itself with `SYS_INIT` and requires no call from `main.c`. This follows ADR-008 (Kconfig-only composition): enabling the feature is a single `prj.conf` line.

### 4. k_spinlock (not k_mutex) for ring-buffer protection

zbus listener callbacks run in the publisher's context. When the publisher is a `k_timer` expiry function, the listener runs from an ISR. A mutex cannot be acquired from ISR context and would cause a kernel panic. A spinlock is ISR-safe and appropriate here because the critical section is short (a `memcpy` of the ring buffer).

### 5. Snapshot pattern

```
acquire spinlock
    memcpy ring buffer → local stack copy
release spinlock

serialize JSON from local copy   ← no lock held
```

The spinlock is held only for the copy; JSON serialisation (which may allocate or block) happens outside the lock. This keeps the critical section as short as possible.

### 6. HTML/JS embedded as C string literals (prototype)

The Chart.js page and config form are stored as C string literals inside `http_dashboard.c`. This approach:

- Works with zero filesystem dependency on `native_sim`.
- Requires no build-time asset pipeline.
- Is sufficient for the prototype phase.

**This is a known limitation.** Editing HTML inside a C string is error-prone and prevents live-reload workflows. The intended target architecture is to serve web assets from LittleFS, keeping HTML/CSS/JS out of C source files entirely (see §Design goal and backlog).

### 7. Iterable section in a linker fragment

Zephyr's HTTP server discovers resource descriptors via a linker-collected iterable section. The library ships `http_dashboard_sections.ld` containing:

```ld
ITERABLE_SECTION_ROM(http_resource_desc_dashboard_svc, 4)
```

This file must be included in the build. Omitting it causes undefined-reference linker errors. The library's `CMakeLists.txt` adds it automatically when `CONFIG_HTTP_DASHBOARD=y`.

### 8. Cross-subsystem coupling rule

When the HTTP dashboard needs to interact with another subsystem (e.g. changing the fake-sensor interval, triggering an SNTP resync), coupling must follow one of two patterns:

- **Preferred:** publish on a zbus channel (e.g. a future `config_event_chan`) so the dashboard remains a pure producer with no direct dependency on the consuming subsystem.
- **Acceptable:** the integration code lives in a dedicated file (e.g. `src/http_dashboard_fake_sensors.c`) compiled only when both `CONFIG_HTTP_DASHBOARD=y` and `CONFIG_FAKE_SENSORS=y`. The core `http_dashboard.c` must not `#include` headers from the other subsystem.

Direct calls from `http_dashboard.c` into another subsystem's public API (as in the current prototype) are a known violation of this rule to be resolved in future iterations.

### 9. Session-cookie + API bearer-token authentication (`CONFIG_HTTP_DASHBOARD_AUTH=y`)

A browser login page (`GET /login`, `POST /login`) validates username and password and issues an `HttpOnly` session cookie. Automation clients send `Authorization: Bearer <token>`. Both mechanisms guard all `/api/*` and `/config` endpoints. Credentials and the API token are persisted in Zephyr settings under `dash/user`, `dash/pass`, and `dash/token`. First-boot defaults are set via Kconfig (`lib/http_dashboard/Kconfig`). When `CONFIG_HTTP_DASHBOARD_AUTH=n` all endpoints remain open — suitable for local development without credentials.

---

## Design goal — HTML decoupling

Embedding HTML as C strings is a prototype convenience. The target architecture is:

1. Web assets (HTML, CSS, JS) live in a dedicated source directory.
2. At build time they are compiled into a LittleFS filesystem image.
3. The C layer becomes an HTTP router only — no HTML in `.c` files.
4. Live-reload becomes possible during development by re-flashing only the filesystem partition.

This work is deferred to a future iteration (see backlog: `[HTTP-DASHBOARD] Decouple HTML/JS from C source via LittleFS`).

---

## Consequences

**Easier:**
- Browser-accessible live sensor data and runtime config — no terminal required.
- Works out of the box on `native_sim`; no hardware, no pairing, no install.
- Kconfig opt-in means zero code and zero RAM cost when disabled.
- Clean separation between data API (`/api/*`) and UI endpoints.

**Harder:**
- HTML/CSS/JS embedded in C strings is difficult to edit and has no live-reload capability.
- The linker fragment `http_dashboard_sections.ld` must be included or the build fails with opaque linker errors.
- When `CONFIG_HTTP_DASHBOARD_AUTH=y`, unauthenticated requests to `/api/*` return 401; integration tests must use the `authed_harness` fixture or supply a bearer token.

**Constrained:**
- `k_spinlock` callbacks must not sleep or block — the ring-buffer copy must complete in bounded time.
- HTML/CSS/JS must be fully self-contained. CDN-hosted libraries (e.g. Chart.js from a CDN) require network access; bundling them inline avoids this but increases firmware size.

---

## Alternatives considered

| Alternative | Rejected because |
|---|---|
| Serial shell only | Requires terminal + shell command knowledge; no visualisation |
| MQTT dashboard (Grafana / Node-RED) | Requires external infrastructure; too heavy for local development |
| LVGL touchscreen config | No remote access; requires physical proximity |
| BLE + companion app | App install required; higher pairing friction |
| LittleFS file serving (now) | Adds filesystem dependency on `native_sim`; deferred to backlog |
