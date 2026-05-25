# CLAUDE.md — weather-station

Weather monitoring firmware for Zephyr RTOS. Sensor nodes (LoRa or UART) transmit
temperature, humidity, pressure, CO2, VOC, and PM2.5 readings to a gateway. The gateway
aggregates events via zbus, displays them on an LVGL screen, serves a live Chart.js
dashboard over HTTP, and publishes to MQTT. All development targets `native_sim` — zero
hardware required. Real hardware (FRDM-MCXN947, LoRa-E5, Wio-E5) is Phase 3.

Every module is a self-contained library under `lib/`, composed via Kconfig, self-wired
via `SYS_INIT`. `main.c` is 3 lines. Libraries communicate only through zbus channels —
no direct calls. Sensors are trigger-driven (no polling, no sensor manager). One event =
one measurement. `env_sensor_data` is a flat 20-byte struct (Q31 fixed-point, no heap).

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.4.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Target** | `native_sim/native/64` |
| **Binary** | `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe` |

## Build, test, commit

```bash
# Always prefix west with ZEPHYR_BASE — env default is stale
ZEPHYR_BASE=/home/zephyr/workspace/zephyr

# Build both apps (pristine after Kconfig/DTS changes)
west build -b native_sim/native/64 apps/gateway
west build -b native_sim/native/64 apps/sensor-node

# Shell smoke-test
printf "help\nfake_sensors list\n" | timeout 10 build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout

# Full test suite
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N

# Lint
pre-commit run --all-files
```

**Gate order:** Build gateway → build sensor-node → shell smoke-test → twister → pre-commit.
Do not skip or reorder. Run `/build-and-test` after every change.

**Agent commands by situation:**

| When | Do |
|---|---|
| Any code change | `/build-and-test` |
| Build/test failure | `/systematic-debug` first, then fix |
| New feature | `/explore-adrs` → `/dev-plan` |
| Before merge | `/review` (5 parallel sub-agents) |
| New ADR or status change | `/arch-sync` |
| Audit subtask code | `/standards-check` |
| New library | `/new-lib <name> "<desc>" <kconfig> <priority>` |
| New fake sensor | `/new-sensor-type <type> "<desc>" <value_milli> <uid>` |
| Branch/commit/PR | See "Agent workflow" below |

## Crucial gotchas

**ZEPHYR_BASE is stale.** Always prefix `west build` and `west twister` with
`ZEPHYR_BASE=/home/zephyr/workspace/zephyr`. If builds fail with cached errors,
delete `CMakeCache.txt` and rebuild pristine.

**No heap.** Never `malloc`, `free`, `k_malloc`, `k_free`. All allocation is static
(stack, BSS, `static`).

**Banned patterns:** `strcpy`, `sprintf`, `gets`, `atoi` | `printk` in `src/` or `lib/`
(test files OK) | Hardcoded I2C/SPI addresses (use `DT_NODELABEL`) | Hardcoded crypto
keys | `.tflite` in CMake/Kconfig

**Bus APIs:** always `_dt()` variants (`i2c_write_read_dt`, `spi_transceive_dt`,
`gpio_dt_spec`). Logging: `LOG_MODULE_REGISTER` + `LOG_INF/WRN/ERR`. Error handling:
return negative `errno`, never silently discard.

**native_sim epoll limits:** One NSOS epoll fd shared across all threads (no mutex).
`CONFIG_ZVFS_POLL_MAX` ≥ 8, `CONFIG_NET_MAX_CONTEXTS` ≥ 16. EPOLL_CTL_ADD EEXIST
(errno=17) crash → add `k_sleep(K_MSEC(CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS))` in
background threads after wake, before opening sockets.
`CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS=200` in test builds. Test pacing: ≥ 0.3s between
HTTP POSTs, ≥ 1.5s after triggering background socket work.

**zbus listeners run in publisher's context** — must not block or sleep. Heavy work
goes in subscriber threads. `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel.

**Fake sensors** (UIDs 0x0001–0x00FF) are production-quality drivers, not stubs. Must
never be `y` in production builds. Real hardware UIDs start at 0x0100.

## Architecture rules

1. One event = one measurement. Flat `env_sensor_data` struct, no heap. [ADR-003]
2. Trigger-driven sampling. No sensor manager, no polling. [ADR-004]
3. `main.c` = `LOG_MODULE_REGISTER` + `return 0`. All logic in libraries. [ADR-008]
4. Kconfig-only app composition. No `target_link_libraries()` in app CMake. [ADR-008]
5. `sensor_uid` is the identity key. Never hardcoded. Use `sensor_registry`. [ADR-003]
6. zbus channel ownership: `ZBUS_CHAN_DEFINE` in one `.c`, `ZBUS_CHAN_DECLARE` in
   the public header. [ADR-002]
7. Integration tests: use harness methods, never raw DUT strings. DUT scope = session.
   Auth tests use `authed_harness` fixture. [ADR-012]

ADRs are frozen once Accepted. Supersede, never edit. Full constraint list:
[`docs/architecture/architecture-constraints.md`](docs/architecture/architecture-constraints.md).

## Agent workflow

1. **Branch** — `git checkout master && git pull && git checkout -b <kebab-name>`
2. **Plan** — `/explore-adrs` → `/dev-plan`
3. **Implement** — smallest change → `/build-and-test` → `/standards-check`
4. **Commit** — `git add <files>` (never `.`). `type(scope): summary ≤72 chars`.
   Types: `feat` `fix` `refactor` `test` `docs` `chore`. Pre-commit must pass.
5. **Review** — `/review` before merge
6. **PR** — `git push -u origin HEAD`; `gh pr create --base master`. Never force-push.

## Design docs

| Document | Content |
|---|---|
| [`docs/adr/`](docs/adr/README.md) | 16 ADRs — WHY decisions were made |
| [`docs/architecture/architecture-constraints.md`](docs/architecture/architecture-constraints.md) | Auto-generated constraint table from all ADRs |
| [`docs/architecture/`](docs/architecture/README.md) | 14 docs — HOW the system works |
| [`docs/backlog.md`](docs/backlog.md) | Deferred features, known violations |

## Library map

All under `lib/`, self-contained, Kconfig-gated, SYS_INIT-wired, zbus-only IPC.

**Channels:** `sensor_trigger_chan` → `sensor_event_chan` → `config_cmd_chan` → `remote_scan_ctrl_chan`

**Key libraries:** `fake_sensors`, `bme680_sensor`, `sen0460_sensor`, `sensor_registry`,
`config_cmd`, `sntp_sync`, `http_dashboard`, `mqtt_publisher`, `lvgl_display`,
`lora_node`, `sensor_node_tx`, `uart_lora_bridge`, `uart_lora_sender`, `pipe_publisher`,
`pipe_transport`, `fota_confirm` (hw-only).

Full catalog: [`docs/architecture/system-overview.md`](docs/architecture/system-overview.md).

**Apps:** `gateway/` (native_sim, frdm_mcxn947), `sensor-node/` (native_sim),
`outdoor_sensor_node/` (lora_e5_mini), `lora_bridge/` (wio_e5_mini).

**Sensor UID ranges (Kconfig):** Fake 0x0001–0x00FF, BME680 0x0011, SEN0460 0x0021.
Broadcast 0xFFFFFFFF. See architecture-constraints.md for full allocation table.
