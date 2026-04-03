# CLAUDE.md — weather-station

Project context for AI coding agents. Read this before writing any code.
Full rationale: [`docs/adr/`](docs/adr/README.md).

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.3.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Target** | `native_sim/native/64` |
| **Container** | devcontainer; Xvfb `:1` + x11vnc port 5900 |

## Essential commands

```bash
west init -l . && west update --narrow    # first-time setup
./.devcontainer/start-display.sh          # start Xvfb + x11vnc
```

Binary: `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe`

Build and test: use `/build-and-test`. Display problems: use `/display-reset`.

## Display / VNC

Xvfb on `:1`, x11vnc → `localhost:5900`, password `zephyr`. Env vars pre-set in `devcontainer.json`. If display hangs, use `/display-reset`.

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.** Temp and humidity are separate `env_sensor_data` events on `sensor_event_chan`.

**2. `env_sensor_data` is a flat 20-byte struct — no pointers, no heap.** Fields: `sensor_uid` (uint32), `type` (enum), `q31_value` (Q31 int32), `timestamp_ms` (int64). See `lib/sensor_event/include/sensor_event/sensor_event.h`.

**3. No sensor manager. No polling. No tight coupling.**
Sensors subscribe to `sensor_trigger_chan`. See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` = `LOG_MODULE_REGISTER` + optional `SYS_INIT` + `k_sleep(K_FOREVER)` only.**
All logic lives in libraries. See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs. Instantiated via `DT_FOREACH_STATUS_OKAY`. Shell-controllable. See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

**6. `sensor_uid` is the identity key.** `sensor_registry` maps uid → metadata. Never hardcode UIDs in consumers.

**7. zbus channel ownership is strict.** `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel; `ZBUS_CHAN_DECLARE` in the public header only. Flow: `trigger_chan → fake_sensor → sensor_event_chan → (consumers)`. See [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md).

**8. Apps configure features via Kconfig only.** No `target_link_libraries()` in app `CMakeLists.txt`. See [ADR-001](docs/adr/ADR-001-repo-and-workspace-structure.md).

**Never create:** a `sensor_manager`, polling loops (`sensor_sample_fetch()`), `CONFIG_BME280`/`CONFIG_SHT4X` real drivers, or files under `.west/` or `build/`.

## Agent workflow (non-negotiable)

1. **Branch first** — `git checkout master && git pull && git checkout -b <kebab-name>`. Never commit to `master`.
2. **Smallest change → build gate** — use `/build-and-test` after every change; fix failures before anything else.
3. **Commit** — use `/git-add` to stage files and commit each logical unit.

## HTTP dashboard (`lib/http_dashboard`)

`CONFIG_HTTP_DASHBOARD=y` in `apps/gateway/prj.conf`. Self-initialises via `SYS_INIT` at APPLICATION 97 — no call from `main.c`.

| Endpoint | Description |
|---|---|
| `GET /` | Live Chart.js timeseries (polls `/api/data`) |
| `GET /config` | Configuration form |
| `GET /api/data` | JSON sensor ring-buffer contents |
| `GET /api/config` | JSON current runtime config (GET) / update (POST) |
| `POST /api/config` | Form-encoded: `trigger_interval_ms`, `sntp_server`, `action=sntp_resync` |

Kconfig: `HTTP_DASHBOARD_PORT` (8080) · `HTTP_DASHBOARD_HISTORY_SIZE` (60) · `HTTP_DASHBOARD_MAX_SENSORS` (16)

**Concurrency:** `k_spinlock` (not mutex) — listener may run from timer-ISR. Snapshot pattern: lock → `memcpy` → unlock → serialize.

**Linker:** `http_dashboard_sections.ld` must be included (`ITERABLE_SECTION_ROM(http_resource_desc_dashboard_svc, ...)`); omitting it causes undefined-reference errors.

To query or update the dashboard at runtime, use `/query-dashboard`.
