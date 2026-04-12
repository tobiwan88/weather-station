# CLAUDE.md — weather-station

Project context for AI coding agents. Full rationale: [`docs/adr/`](docs/adr/README.md).

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.3.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Target** | `native_sim/native/64` |
| **Container** | devcontainer; Xvfb `:1` + x11vnc port 5900 |

## Essential commands

```bash
west init -l . && west update --narrow
./.devcontainer/start-display.sh
```

Binary: `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe`

Build and test: `/build-and-test`. Display problems: `/display-reset`.

Integration tests: `/run-integration-tests [marker]`. New test: `/new-integration-test`. New harness: `/new-harness`.

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.** Temp and humidity are separate `env_sensor_data` events.

**2. `env_sensor_data` is a flat 20-byte struct — no pointers, no heap.** Fields: `sensor_uid` (uint32), `type` (enum), `q31_value` (Q31 int32), `timestamp_ms` (int64).

**3. No sensor manager. No polling. No tight coupling.** Sensors subscribe to `sensor_trigger_chan`. See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` = `LOG_MODULE_REGISTER` + `k_sleep(K_FOREVER)` only.** All logic lives in libraries, self-wired via `SYS_INIT`. See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs. Instantiated via `DT_FOREACH_STATUS_OKAY`. See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

**6. `sensor_uid` is the identity key.** `sensor_registry` maps uid → metadata. Never hardcode UIDs in consumers.

**7. zbus channel ownership is strict.** `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel; `ZBUS_CHAN_DECLARE` in the public header only. Channels: `sensor_trigger_chan`, `sensor_event_chan`, `config_cmd_chan`, `remote_discovery_chan`, `remote_scan_ctrl_chan`. See [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md).

**8. Apps configure features via Kconfig only.** No `target_link_libraries()` in app `CMakeLists.txt`. See [ADR-001](docs/adr/ADR-001-repo-and-workspace-structure.md).

**Never create:** a `sensor_manager`, polling loops, real hw drivers (`CONFIG_BME280` etc.), or files under `.west/` or `build/`.

## Agent workflow (non-negotiable)

1. **Branch first** — `git checkout master && git pull && git checkout -b <kebab-name>`. Never commit to `master`.
2. **Smallest change → build gate** — `/build-and-test` after every change; fix failures before anything else.
3. **Commit** — `/git-add` to stage, commit each logical unit. Use Conventional message style. Fix issues found by pre-commit.
4. **Review** — `/review` to spawn parallel sub-agents (architecture, security, C quality, embedded, tests) reviewing the patch from different angles.
5. **PR** — `/open-pr` to push, create PR, watch CI, and fix failures until green.

## HTTP dashboard (`lib/http_dashboard`)

`CONFIG_HTTP_DASHBOARD=y`. Self-initialises via `SYS_INIT` at APPLICATION 97.

| Endpoint | Description |
|---|---|
| `GET /` | Live Chart.js timeseries |
| `GET /config` | Configuration form |
| `GET /api/data` | JSON sensor ring-buffer |
| `POST /api/config` | Form-encoded: `trigger_interval_ms`, `sntp_server`, `action=sntp_resync` |

- **Config decoupling:** POST publishes `config_cmd_event` on `config_cmd_chan` — does NOT call `fake_sensors`/`sntp_sync` directly.
- **Concurrency:** `k_spinlock` + snapshot pattern (lock → memcpy → unlock → serialize).
- **Linker:** `http_dashboard_sections.ld` required (`ITERABLE_SECTION_ROM(http_resource_desc_dashboard_svc, ...)`).

## Key libraries

| Library | Notes |
|---|---|
| `config_cmd` | Owns `config_cmd_chan`. Commands: `CONFIG_CMD_SET_TRIGGER_INTERVAL` (arg=ms), `CONFIG_CMD_SNTP_RESYNC`. |
| `location_registry` | Runtime CRUD for named locations. `location_registry_add/remove/exists/foreach`. Settings-persisted (`loc/`). |
| `sensor_event_log` | No public API. Self-registers via `SYS_INIT`. Enable: `CONFIG_SENSOR_EVENT_LOG=y`. |
| `remote_sensor` | Transport vtable (`REMOTE_TRANSPORT_DEFINE()`). Needs `remote_sensor_iterables.ld`. UID helpers: `remote_sensor_uid_from_addr()`, `remote_sensor_uid_from_node_id()`. Publish: `remote_sensor_publish_data(uid, type, q31)`. |
| `fake_remote_sensor` | Testing stub implementing `remote_transport` vtable (`REMOTE_TRANSPORT_PROTO_FAKE`). |

## Integration tests (`tests/integration`)

Pytest-based tests via Twister's `harness: pytest`. Boots the full gateway
stack on `native_sim/native/64` (no LVGL) and interacts through three surfaces:

| Surface | Harness class | Fixture |
|---|---|---|
| UART shell | `ShellHarness` | `shell_harness` |
| HTTP API (port 8080) | `HttpHarness` | `http_harness` |
| MQTT (port 1883) | `MqttHarness` | `mqtt_harness` (auto-skips if no broker) |

- **Page Object Model:** tests call harness methods (`shell_harness.list_sensors()`), never raw strings.
- **Markers:** `smoke`, `shell`, `http`, `mqtt`, `e2e` — filter with `--pytest-args="-m smoke"`.
- **DUT scope = session:** one boot per suite; tests must restore state after mutations.
- **ZEPHYR_BASE override required:** `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister ...` (shell env var is stale).

See [ADR-012](docs/adr/ADR-012-integration-test-architecture.md).
