# CLAUDE.md — weather-station

Project context for AI coding agents.

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.4.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Target** | `native_sim/native/64` |
| **Container** | devcontainer; Xvfb `:1` + x11vnc port 5900 |

## Essential commands

```bash
west init -l . && west update --narrow
west patch apply
./.devcontainer/start-display.sh
```

Binary: `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe`

**CRITICAL — ZEPHYR_BASE:** Always prefix `west build` and `west twister` with `ZEPHYR_BASE=/home/zephyr/workspace/zephyr`. If builds fail with a stale path, delete `CMakeCache.txt`.

## Agent inventory — what to invoke when

| Situation | Command |
|---|---|
| Code, Kconfig, DTS, or config changed | `/build-and-test` |
| Build or test failure — before fixing | `/systematic-debug` |
| Before implementing any feature | `/explore-adrs` then `/dev-plan` |
| Code completed, pre-merge | `/review` |
| Architectural decision needed | `/adr [topic]` |
| ADR status changed or new ADR created | `/arch-sync` |
| Audit changed code (per-subtask) | `/standards-check` |
| Add a library under lib/ | `/new-lib` |
| Add a fake sensor type | `/new-sensor-type` |
| Add or update diagrams | `/new-diagram` |
| Apply a Zephyr out-of-tree patch | `/west-patch` |

## Design information

| Document | Purpose |
|---|---|
| [`docs/adr/`](docs/adr/README.md) | Architecture decisions — WHY |
| [`docs/architecture/architecture-constraints.md`](docs/architecture/architecture-constraints.md) | Auto-generated constraint summary from all ADRs — read first |
| [`docs/architecture/`](docs/architecture/README.md) | System design — HOW |
| [`docs/backlog.md`](docs/backlog.md) | Deferred features, known violations |

ADRs are frozen once Accepted. Supersede, never edit.

## Architecture rules (non-negotiable)

1. **One event = one physical measurement.** Temp and humidity are separate `env_sensor_data` events. [ADR-003](docs/adr/ADR-003-sensor-event-data-model.md)
2. **`env_sensor_data` is a flat struct — no pointers, no heap.** 20/24 bytes (32/64-bit). [ADR-003]
3. **No sensor manager. No polling. No tight coupling.** Sensors subscribe to `sensor_trigger_chan`. [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md)
4. **`main.c` = `LOG_MODULE_REGISTER` + `return 0` only.** All logic in libraries, self-wired via `SYS_INIT`. [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md)
5. **Fake sensors are production-quality drivers**, not stubs. DT-defined, shell-interactive. [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md)
6. **`sensor_uid` is the identity key.** Never hardcode UIDs in consumers — use `sensor_registry`. [ADR-003]
7. **zbus channel ownership is strict.** `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel; `ZBUS_CHAN_DECLARE` in the public header only. [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md)
8. **Apps compose features via Kconfig only.** No `target_link_libraries()` in app `CMakeLists.txt`. [ADR-008]

**Never create:** a `sensor_manager`, polling loops, files under `.west/` or `build/`.

## Banned patterns

`strcpy`, `sprintf`, `gets`, `atoi` | `printk` in `src/` or `lib/` | `malloc`/`free`/`k_malloc`/`k_free` (no heap) | Hardcoded I2C/SPI addresses (use `DT_NODELABEL`) | Hardcoded crypto keys | `.tflite` in CMake/Kconfig

## Coding rules

- Bus APIs: always `_dt()` variants (`i2c_write_read_dt`, `spi_transceive_dt`, `gpio_dt_spec`)
- Logging: `LOG_MODULE_REGISTER` + `LOG_INF/WRN/ERR` — never `printk` in `src/`
- Error handling: return negative `errno` — never silently discard
- Assertions: `BUILD_ASSERT` for compile-time, `__ASSERT` for internal invariants, `if`/error-return for external input

## Agent workflow

1. **Branch first** — `git checkout master && git pull && git checkout -b <kebab-name>`. Never commit to `master`.
2. **Plan** — `/explore-adrs` before any feature. `/dev-plan` to decompose into file-allowlisted subtasks.
3. **Implement** — smallest change possible. `/build-and-test` after every change; `/standards-check` after every subtask. Fix failures before anything else.
4. **Commit** — `git add <files>` only, never `git add .`. Format: `type(scope): imperative summary ≤72 chars`. Types: `feat` `fix` `refactor` `test` `docs` `chore`. Run `pre-commit run --all-files`. Never `--no-verify`.
5. **Review** — `/review` spawns 5 parallel sub-agents (architecture, security, C quality, embedded, tests).
6. **PR** — `git push -u origin HEAD`; `gh pr create --base master`. Title ≤70 chars. Never force-push.

## Library catalog

All libraries under `lib/` are self-contained, Kconfig-gated, self-wire via `SYS_INIT`. They communicate through zbus channels only — never direct calls between libraries (except public read-only APIs: `sensor_registry`, `location_registry`).

### Core channels

| Channel | Owner | Message type |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | `sensor_trigger_event` |
| `sensor_event_chan` | `lib/sensor_event` | `env_sensor_data` |
| `config_cmd_chan` | `lib/config_cmd` | `config_cmd_event` |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | `remote_scan_ctrl_event` |

### Sensor UID allocation (Kconfig, not DT)

| Type | Kconfig | Default | Range |
|---|---|---|---|
| Temperature | `FAKE_TEMPERATURE_UID_BASE` | 0x0001 | Base + {0..N-1} |
| Humidity | `FAKE_HUMIDITY_UID_BASE` | 0x0003 | Base + {0..N-1} |
| CO2 | `FAKE_CO2_UID_BASE` | 0x0005 | Base + {0..N-1} |
| VOC | `FAKE_VOC_UID_BASE` | 0x0006 | Base + {0..N-1} |
| BME680 | `BME680_SENSOR_DEFAULT_UID` | 0x0011 | single |
| SEN0460 | `SEN0460_SENSOR_DEFAULT_UID` | 0x0021 | single |
| Broadcast | `HW_SENSOR_BROADCAST_UID` | 0xFFFFFFFF | constant |

Fake sensors: UIDs 0x0001–0x00FF. Real hardware: starts at 0x0100.

### Key libraries (full catalog in [`docs/architecture/system-overview.md`](docs/architecture/system-overview.md))

| Domain | Key libraries |
|---|---|
| **Sensors** | `fake_sensors`, `bme680_sensor`, `sen0460_sensor`, `hw_sensor_utils` |
| **Service** | `sensor_registry`, `config_cmd`, `sntp_sync`, `location_registry`, `fota_confirm` (hw-only) |
| **Output** | `http_dashboard`, `mqtt_publisher`, `lvgl_display`, `sensor_event_log` |
| **LoRa** | `lora_node`, `sensor_node_tx`, `uart_lora_bridge`, `uart_lora_sender`, `lora_radio` |
| **Test** | `pipe_publisher`, `pipe_transport`, `fake_remote_sensor` |

### Apps

| App | Target | Role |
|---|---|---|
| `gateway/` | `native_sim`, `frdm_mcxn947` | Main firmware: aggregation, HTTP, MQTT, LVGL, LoRa RX |
| `sensor-node/` | `native_sim` | LoRa TX beacon for integration testing |
| `outdoor_sensor_node/` | `lora_e5_mini` | BME688 + LoRa TX + PM sleep |
| `lora_bridge/` | `wio_e5_mini` | STM32WLE5JC LoRa co-processor → UART bridge |

## Integration tests

Pytest via Twister `harness: pytest`. Markers: `smoke`, `shell`, `http`, `mqtt`, `e2e`, `system`. DUT scope = session (one boot per suite). Use harness methods — never raw DUT strings. Auth tests: `authed_harness` fixture when `CONFIG_HTTP_DASHBOARD_AUTH=y`.

### native_sim socket constraints — hard limits

`native_sim/native/64` shares one NSOS epoll fd across all POSIX threads (no mutex):

| Config | Value | Why |
|---|---|---|
| `CONFIG_ZVFS_POLL_MAX` | ≥ 8 | HTTP server needs 5 poll slots |
| `CONFIG_NET_MAX_CONTEXTS` | ≥ 16 | HTTP + MQTT + SNTP exhaust default 6 |
| `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS` | 200 (tests) | Prevents EPOLL_CTL_ADD EEXIST (errno=17) race |

**EEXIST crash:** `error in EPOLL_CTL_ADD: errno=17` → add `k_sleep(K_MSEC(presync_delay))` in background threads after wake, before opening sockets.

**Test pacing:** ≥ 0.3s between HTTP POSTs. ≥ 1.5s after triggering background socket work (SNTP resync, remote scan).
