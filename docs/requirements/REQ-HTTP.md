# REQ-HTTP — HTTP Dashboard Requirements

## Status
review

## Version
0.1

## Context
The HTTP dashboard provides a web interface for live sensor data visualization (Chart.js timeseries), runtime configuration, authentication, and FOTA image upload. It runs on port 8080 and is Kconfig-opt-in.

## Functional Requirements

### REQ-HTTP-001 — Dashboard Server
The system shall serve an HTTP dashboard on port 8080 (configurable via `HTTP_DASHBOARD_PORT`), self-initialized via `SYS_INIT` at priority 97.

### REQ-HTTP-002 — Chart.js Timeseries
The system shall display live sensor data as Chart.js timeseries on the dashboard homepage (`GET /`), polling `/api/data` every 1 second.

### REQ-HTTP-003 — REST API Endpoints
The system shall provide the following REST API endpoints:
- `GET /api/data` — JSON snapshot of ring buffer
- `GET/POST /api/config` — Read/update runtime config
- `POST /api/login` — Authenticate, return session cookie
- `POST /api/logout` — Invalidate session
- `POST /api/change-credentials` — Update username/password
- `POST /api/token/rotate` — Rotate API bearer token
- `GET /api/locations` — JSON list of locations

### REQ-HTTP-004 — Session Authentication
The system shall require session-cookie or API bearer-token authentication for `/api/*` endpoints. Unauthenticated requests shall return 401.

### REQ-HTTP-005 — Configuration Form
The system shall serve an HTML configuration form at `GET /config` for runtime configuration of MQTT, locations, and other settings.

### REQ-HTTP-006 — FOTA Upload Endpoint
The system shall accept firmware image uploads via `POST /api/fota/upload`, streaming to flash slot 1. Additional endpoints:
- `POST /api/fota/apply` — Mark slot 1 pending + reboot
- `GET /api/fota/status` — Return slot versions and state
- `POST /api/fota/sensor/<uid>/apply` — Trigger sensor-node FOTA over LoRa

### REQ-HTTP-007 — Self-Contained Assets
The system shall serve HTML/CSS/JS as self-contained content with no CDN dependencies. Assets shall be embedded as C string literals (prototype) or served from LittleFS (target architecture).

### REQ-HTTP-008 — Ring Buffer Snapshot
The system shall protect the sensor data ring buffer with `k_spinlock`, copying the snapshot within the lock and serializing to JSON outside the lock.

### REQ-HTTP-009 — Kconfig Opt-In
The system shall be enabled via `CONFIG_HTTP_DASHBOARD=y` and disabled by default.

## Dependencies
- REQ-FOTA-001: MCUboot slot management for FOTA upload
- REQ-CONFIG-001: config_cmd channel for runtime config
- REQ-LOCATION-001: location registry for `/api/locations`
- REQ-DATA-003: ring buffer snapshot pattern

## Constraints
- HTTP server needs ~1 second after boot to bind port 8080
- `k_spinlock` callbacks must not sleep or block
- HTML embedded in C strings is a prototype limitation; target is LittleFS
- Cross-subsystem coupling: prefer zbus publish; conditional integration files acceptable

## Acceptance Criteria
- [ ] [native_sim] Integration: HTTP server binds port 8080 within 1s of boot
- [ ] [native_sim] Integration: GET / returns HTML page with Chart.js
- [ ] [native_sim] Integration: GET /api/data returns JSON sensor data snapshot
- [ ] [native_sim] Integration: POST /api/login returns session cookie
- [ ] [native_sim] Integration: Unauthenticated GET /api/data returns 401
- [ ] [native_sim] Integration: Authenticated GET /api/data returns 200 with data
- [ ] [native_sim] Integration: POST /api/fota/upload streams image to flash
- [ ] [native_sim] Integration: GET /api/config returns configuration form
- [ ] [native_sim] Integration: POST /api/config updates runtime config
- [ ] [renode] Integration: HTTP endpoints functional over simulated network
- [ ] [manual] Board: Dashboard accessible from browser on local network

## Related ADRs
- ADR-011: HTTP Dashboard Web Interface
- ADR-014: MCUboot FOTA and Secure Firmware Update
- ADR-002: zbus as System-Wide Communication Fabric

## Related Zephyr subsystems
- CONFIG_HTTP_SERVER
- CONFIG_NET_TCP
- CONFIG_NET_SOCKETS
- CONFIG_FLASH
- CONFIG_LITTLEFS
