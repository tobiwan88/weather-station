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
west patch apply          # apply all patches in zephyr/patches.yml
./.devcontainer/start-display.sh
```

Binary: `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe`

Renode simulation (hardware): `simulation/renode/` — Robot Framework tests for MCXN947 boot and FOTA. Install: `.devcontainer/install-renode.sh`.

Build and test: `/build-and-test`. Integration tests: `/run-integration-tests [marker]`. New library: `/new-lib`. New sensor type: `/new-sensor-type`. New ADR: `/adr [topic]`.

**CRITICAL — ZEPHYR_BASE:** Always prefix `west build` and `west twister` with `ZEPHYR_BASE=/home/zephyr/workspace/zephyr`; the env default points to a non-existent path. If builds fail with a stale path, delete `CMakeCache.txt`.

## Zephyr patches (`west patch`)

Out-of-tree fixes to Zephyr are managed as `git format-patch` files in
`zephyr/patches/zephyr/`, tracked in `zephyr/patches.yml`, and applied with
`west patch apply` (run after `west update`). Use `west patch clean` before
`west update` to revert.

To add a new patch: `/west-patch`.

**Do NOT** edit Zephyr source directly without a patch entry — changes are lost on `west patch clean` / `west update`.

## Where to find design information

| Document type | Location | Purpose |
|---|---|---|
| **ADRs** (why) | [`docs/adr/`](docs/adr/README.md) | Architectural decisions: context, rationale, alternatives, consequences |
| **Architecture docs** (how) | [`docs/architecture/`](docs/architecture/README.md) | System overview, event bus, composition model, concurrency, test architecture |
| **Diagrams** (visual) | [`docs/architecture/diagrams/`](docs/architecture/diagrams/) | 10 `.mmd` source files. Add or update with `/new-diagram`. |
| **Backlog** | [`docs/backlog.md`](docs/backlog.md) | Deferred features, known violations, future work |

Run `/explore-adrs` before implementing any feature — it reads the ADR index and surfaces the relevant decisions. ADRs are frozen; write a new one for new decisions (`/adr`).

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.** Temp and humidity are separate `env_sensor_data` events. See [ADR-003](docs/adr/ADR-003-sensor-event-data-model.md).

**2. `env_sensor_data` is a flat struct — no pointers, no heap.** Fields: `sensor_uid` (uint32), `type` (enum), `q31_value` (Q31 int32), `timestamp_ms` (int64). Size: 20 bytes on 32-bit, 24 bytes on 64-bit (padding before `int64_t`).

**3. No sensor manager. No polling. No tight coupling.** Sensors subscribe to `sensor_trigger_chan`. See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` = `LOG_MODULE_REGISTER` + `return 0` only.** All logic lives in libraries, self-wired via `SYS_INIT`. Zephyr keeps running after `main()` returns. See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs. Instantiated via `LISTIFY` + `DT_INST` from DT nodes. UIDs assigned via Kconfig bases. See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

**6. `sensor_uid` is the identity key.** `sensor_registry` maps uid → metadata. Never hardcode UIDs in consumers.

**7. zbus channel ownership is strict.** `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel; `ZBUS_CHAN_DECLARE` in the public header only. Channels: `sensor_trigger_chan`, `sensor_event_chan`, `config_cmd_chan`, `remote_scan_ctrl_chan`. Discovery events use `k_msgq` (not zbus) inside `remote_sensor_manager`. See [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md).

**8. Apps configure features via Kconfig only.** No `target_link_libraries()` in app `CMakeLists.txt`. See [ADR-001](docs/adr/ADR-001-repo-and-workspace-structure.md).

**Never create:** a `sensor_manager`, polling loops, real hw drivers (`CONFIG_BME280` etc.), or files under `.west/` or `build/`.

## Embedded C coding rules (non-negotiable)

**No heap.** Never use `malloc`, `free`, `k_malloc`, `k_free`, or any dynamic allocator. All buffers and objects are statically allocated (stack, BSS, or `static`).

**Assertions by origin:**

| Check type | Macro | When to use |
|---|---|---|
| Static invariant | `BUILD_ASSERT(cond, msg)` | Compile-time constants, struct sizes, Kconfig ranges |
| Internal runtime invariant | `__ASSERT(cond, fmt, ...)` | Preconditions on values that come from within the system (own APIs, zbus messages, DT-derived data) |
| External input | Normal `if`/error return | Data from the web API, shell commands, MQTT payloads, or any untrusted source — never assert, always validate and return an error |

`__ASSERT` is fatal in debug builds; it must never fire on well-formed external input.

## Agent workflow (non-negotiable)

1. **Branch first** — `git checkout master && git pull && git checkout -b <kebab-name>`. Never commit to `master`. This applies to **all** commits — design specs, documentation, config changes, and code alike. No exceptions.
2. **Smallest change → build gate** — `/build-and-test` after every change; fix failures before anything else.
3. **Commit** — Stage explicit files (`git add <files>`, never `git add .`). Commit each logical unit: `type(scope): imperative summary ≤72 chars`. Types: `feat` `fix` `refactor` `test` `docs` `chore`. Scope: library or app name (e.g. `fake_sensors`, `http_dashboard`). Run `pre-commit run --all-files` last. Never skip hooks (`--no-verify`).
4. **Review** — `/review` to spawn parallel sub-agents (architecture, security, C quality, embedded, tests) reviewing the patch from different angles.
5. **PR** — `git push -u origin HEAD`, then `gh pr create --base master`. Title: same Conventional Commits format, ≤70 chars. CI failures: fix locally, new commit (never amend published), re-push. Never force-push.

**Diagrams:** add new diagrams as `.mmd` files in `docs/architecture/diagrams/`; embed in the relevant arch page with ` ```mermaid\n--8<-- "name.mmd"\n``` `; add to `diagrams.md` catalog. Do NOT add inline Mermaid code blocks directly in arch pages. Do NOT use the `mermaid2` MkDocs plugin — use `pymdownx.superfences` (already configured).

## Library catalog

All libraries under `lib/` are self-contained, Kconfig-gated, and self-wire via `SYS_INIT`. They communicate through zbus channels — never by calling each other's internal functions. Public read-only APIs (`sensor_registry`, `location_registry`) may be called by any library.

### Core channels

| Channel | Owner | Message type |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | `sensor_trigger_event` |
| `sensor_event_chan` | `lib/sensor_event` | `env_sensor_data` |
| `config_cmd_chan` | `lib/config_cmd` | `config_cmd_event` |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | `remote_scan_ctrl_event` |

### Sensor layer

| Library | Kconfig | Role |
|---|---|---|
| `fake_sensors` | `CONFIG_FAKE_SENSORS` | DT-driven fake drivers (temp, humidity, CO2, VOC). Subscribes trigger chan, publishes event chan. Shell: `fake_sensors list/set/trigger`. Auto-publish timer at `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS`. UIDs assigned via Kconfig: `CONFIG_FAKE_TEMPERATURE_UID_BASE`, `CONFIG_FAKE_HUMIDITY_UID_BASE`, etc. |
| `sensor_registry` | `CONFIG_SENSOR_REGISTRY` | Runtime uid → metadata map. Sensors self-register at boot. User metadata via `CONFIG_SENSOR_REGISTRY_USER_META`: `sensor_registry_set_meta/get_meta/get_display_name/get_location`. Settings-persisted. |
| `remote_sensor` | `CONFIG_REMOTE_SENSOR` | Transport-agnostic abstraction for wireless sensors. Vtable pattern (`REMOTE_TRANSPORT_DEFINE()`), manager thread, UID derivation. Needs `remote_sensor_iterables.ld`. Shell: `remote_sensor list/scan/pair/unpair`. |
| `fake_remote_sensor` | `CONFIG_FAKE_REMOTE_SENSOR` | Testing stub implementing `remote_transport` vtable (`REMOTE_TRANSPORT_PROTO_FAKE`). |
| `hw_sensor_utils` | `CONFIG_HW_SENSOR_UTILS` | Shared `hw_sensor_publish()` helper for hardware sensor drivers. No state, no SYS_INIT — pure utility. |
| `bme680_sensor` | `CONFIG_BME680_SENSOR` | BME680/BME688 hardware driver. Subscribes trigger chan, reads T/H/P/gas via Zephyr sensor API, publishes event chan. `pm_device` integration, power optimization Kconfig hooks (ODR, heater profile, forced mode). Public API: `bme680_sensor_enable()/disable()`. |
| `sen0460_sensor` | `CONFIG_SEN0460_SENSOR` | SEN0460 PM2.5 stub (DT binding, Kconfig, skeleton). Returns `-ENOSYS` on read — proves architecture. Functional driver deferred. |

**Sensor UID allocation** (assigned via Kconfig, not DT):

| Type | Kconfig | Default | Instance range |
|---|---|---|---|
| Temperature | `CONFIG_FAKE_TEMPERATURE_UID_BASE` | `0x0001` | `BASE + {0..N-1}` |
| Humidity | `CONFIG_FAKE_HUMIDITY_UID_BASE` | `0x0003` | `BASE + {0..N-1}` |
| CO2 | `CONFIG_FAKE_CO2_UID_BASE` | `0x0005` | `BASE + {0..N-1}` |
| VOC | `CONFIG_FAKE_VOC_UID_BASE` | `0x0006` | `BASE + {0..N-1}` |
| BME680 | `CONFIG_BME680_SENSOR_DEFAULT_UID` | `0x0011` | single instance |
| SEN0460 | `CONFIG_SEN0460_SENSOR_DEFAULT_UID` | `0x0021` | single instance |
| Broadcast | `HW_SENSOR_BROADCAST_UID` | `0xFFFFFFFF` | constant |

Override per-app in `prj.conf` (e.g. `CONFIG_FAKE_TEMPERATURE_UID_BASE=0x0021` for sensor-node).
Location semantics come from `sensor_registry` metadata, not UID ranges.

### Services

| Library | Kconfig | Role |
|---|---|---|
| `config_cmd` | `CONFIG_CONFIG_CMD` | Owns `config_cmd_chan`. Commands: `SET_TRIGGER_INTERVAL`, `SNTP_RESYNC`, `MQTT_SET_ENABLED`, `MQTT_SET_BROKER`, `MQTT_SET_AUTH`, `MQTT_SET_GATEWAY`. |
| `sntp_sync` | `CONFIG_SNTP_SYNC` | Wall-clock SNTP sync at boot + periodic. Dedicated thread. Subscribes config_cmd_chan for resync requests. |
| `location_registry` | `CONFIG_LOCATION_REGISTRY` | Runtime CRUD for named locations. `add/remove/exists/foreach`. Settings-persisted (`loc/`). Shell: `location add/remove/list`. |
| `clock_display` | `CONFIG_CLOCK_DISPLAY` | Logs HH:MM UTC every 60s via delayable work item. |
| `sensor_event_log` | `CONFIG_SENSOR_EVENT_LOG` | No public API. Self-registers via `SYS_INIT`. Logs every sensor event to console. |
| `fota_confirm` | `CONFIG_FOTA_CONFIRM` | Confirms the running MCUboot image `CONFIG_FOTA_CONFIRM_DELAY_S` seconds (default 5) after `SYS_INIT APPLICATION 99`. Hardware only — `depends on BOOTLOADER_MCUBOOT`. Do NOT enable in native_sim builds. |

### Output / connectivity

| Library | Kconfig | Role |
|---|---|---|
| `http_dashboard` | `CONFIG_HTTP_DASHBOARD` | Web dashboard on port 8080. Chart.js timeseries, config page, auth (session cookie + bearer token). Self-init at APPLICATION 97. POST `/api/config` publishes on `config_cmd_chan` — does NOT call other libraries directly. Spinlock + snapshot pattern for ring buffer. Linker: `http_dashboard_sections.ld`. FOTA routes (`/api/fota/upload\|apply\|status`) via `CONFIG_HTTP_DASHBOARD_FOTA` (hardware only). |
| `lora_node` | `CONFIG_LORA_NODE` | LoRa TX for sensor-node apps. Packet encoding (L2 header + AES-GCM + CRC-16). Public API: `lora_node_init()`, `lora_node_transmit(readings, count)`, `lora_node_get_id()`, `lora_node_save_session()`. Depends on PSA_CRYPTO. |
| `lvgl_display` | `CONFIG_LVGL_DISPLAY` | SDL 320×240 window. Analog clock + sensor cards. Subscribes event chan. `lvgl_display_run()` blocks on main thread (known ADR-008 violation, tracked in backlog). |
| `mqtt_publisher` | `CONFIG_MQTT_PUBLISHER` | Subscribes event chan. Topic: `{gw}/{location}/{display_name}/{type}`. Settings under `config/mqtt/` (server, port, user, pass, gw). Passwords base64-encoded. Shell: `mqtt_pub status/set`. Use `zsock_pollfd`/`zsock_poll()`/`ZSOCK_POLLIN` — not POSIX variants. |
| `pipe_publisher` | `CONFIG_PIPE_PUBLISHER` | Writes `env_sensor_data` as length-prefixed protobuf to POSIX FIFO. Sensor-node side for integration testing. |
| `pipe_transport` | `CONFIG_PIPE_TRANSPORT` | Reads from POSIX FIFO, decodes protobuf, publishes to `sensor_event_chan`. Gateway side for integration testing. |
| `sensor_node_tx` | `CONFIG_SENSOR_NODE_TX` | Subscribes `sensor_event_chan`, accumulates readings in ring buffer, transmits batched 5-byte LoRa frames on timer or force-TX command. Shell: `sensor_node_tx force/enable/disable/status`. PM-ready: `enable()`/`disable()` lifecycle. |
| `uart_lora_bridge` | `CONFIG_UART_LORA_BRIDGE` | Receives 24-byte wire frames from LoRa co-processor over UART, converts to `env_sensor_data`, publishes to `sensor_event_chan`. Gateway side. ISR → msgq → thread state machine. |
| `uart_lora_sender` | `CONFIG_UART_LORA_SENDER` | Subscribes `sensor_event_chan`, packs events into 24-byte wire frames (magic 0x5A 0xA5, length, payload, CRC8), sends over UART via `uart_poll_out()`. Co-processor side. |

### Apps

| App | Target | Role |
|---|---|---|
| `apps/gateway/` | `native_sim`, `frdm_mcxn947` | Main gateway firmware: sensor aggregation, HTTP dashboard, MQTT, LVGL display, LoRa RX via UART bridge. |
| `apps/sensor-node/` | `native_sim` | LoRa TX beacon for integration testing. |
| `apps/outdoor_sensor_node/` | `lora_e5_mini` | Outdoor sensor node: BME688 sensor via I2C, LoRa TX, PM sleep between cycles. |
| `apps/lora_bridge/` | `wio_e5_mini` | Wio-E5 Mini (STM32WLE5JC) LoRa co-processor firmware. Runs `lib/lora_radio/` with real SX126x hardware, forwards decoded sensor data to gateway via UART. |

## Integration tests (`tests/integration`)

Pytest via Twister `harness: pytest`. Full gateway on `native_sim/native/64`. Build/run commands: see [`README.md`](README.md#testing).

- **Never raw strings.** Tests call harness methods (`ShellHarness`, `HttpHarness`, `MqttHarness`).
- **Markers:** `smoke`, `shell`, `http`, `mqtt`, `e2e`, `system`.
- **DUT scope = session:** one boot per suite; restore state after mutations.
- **Auth tests:** use `authed_harness` fixture for all `POST /api/config` tests when `CONFIG_HTTP_DASHBOARD_AUTH=y`.
- **ZEPHYR_BASE override required:** `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister ...`
- **MQTT broker:** `mosquitto -p 1883 -d`; tests auto-skip if none running.
- **New test file:** `tests/integration/pytest/test_<topic>.py`; session-scoped harness fixtures, never raw DUT strings.
- **Extend vs. create harness:** add a method to an existing harness for new shell sub-commands or HTTP endpoints; create a new harness class only for a new interaction surface. Files: `tests/integration/pytest/harnesses/<name>_harness.py`; register in `conftest.py`.

### native_sim/native/64 socket constraints

`native_sim/native/64` runs every Zephyr thread as a real POSIX thread sharing
**one NSOS epoll fd** (no mutex). This creates hard constraints:

| Config | Required value | Why |
|---|---|---|
| `CONFIG_ZVFS_POLL_MAX` | ≥ 8 | HTTP server needs 5 poll slots (1 eventfd + 1 listen + 3 clients); default 3 causes partial-prepare leaving stale epoll entries |
| `CONFIG_NET_MAX_CONTEXTS` | ≥ 16 | One context per open socket; default 6 is exhausted by HTTP + MQTT + SNTP |
| `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS` | 200 (test build) | SNTP thread and HTTP server thread run on separate CPUs; without a settling delay SNTP's `epoll_ctl ADD` races the HTTP server's in-flight response send and gets `EEXIST` → fatal exit |

**EEXIST crash pattern** — if `handler.log` ends with `error in EPOLL_CTL_ADD: errno=17`
right after an HTTP POST that triggers background socket work (SNTP resync, remote scan,
etc.), the cause is a concurrent `epoll_ctl ADD` collision. Fix: add
`k_sleep(K_MSEC(CONFIG_..._PRESYNC_DELAY_MS))` in the background thread after waking,
before opening any socket. The delay goes in the triggered path only (not the boot sync).

**Test pacing** — rapid HTTP POST sequences exhaust the server's small connection pool.
Pace every request pair with at least `time.sleep(0.3)`. After triggering a background
operation that opens a socket (SNTP resync, scan), sleep long enough to cover the
operation's worst-case duration: `presync_delay + timeout + buffer` (1.5 s for SNTP).

See [ADR-012](docs/adr/ADR-012-integration-test-architecture.md) and [`docs/architecture/integration-tests.md`](docs/architecture/integration-tests.md).
