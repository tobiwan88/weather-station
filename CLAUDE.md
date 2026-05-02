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

Build and test: `/build-and-test`. Integration tests: `/run-integration-tests [marker]`. New test: `/new-integration-test`. New harness: `/new-harness`. New library: `/new-lib`. New sensor instance: `/add-sensor-instance`.

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
| **Diagrams** (visual) | [`docs/architecture/diagrams/`](docs/architecture/diagrams/) | Component view, channel map, data flow, library deps, init sequence, HTTP flow |
| **Backlog** | [`docs/backlog.md`](docs/backlog.md) | Deferred features, known violations, future work |

Always read the relevant ADRs before implementing a feature (e.g. ADR-003 + ADR-004 before any sensor driver work).

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.** Temp and humidity are separate `env_sensor_data` events. See [ADR-003](docs/adr/ADR-003-sensor-event-data-model.md).

**2. `env_sensor_data` is a flat struct — no pointers, no heap.** Fields: `sensor_uid` (uint32), `type` (enum), `q31_value` (Q31 int32), `timestamp_ms` (int64). Size: 20 bytes on 32-bit, 24 bytes on 64-bit (padding before `int64_t`).

**3. No sensor manager. No polling. No tight coupling.** Sensors subscribe to `sensor_trigger_chan`. See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` = `LOG_MODULE_REGISTER` + `return 0` only.** All logic lives in libraries, self-wired via `SYS_INIT`. Zephyr keeps running after `main()` returns. See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs. Instantiated via `DT_FOREACH_STATUS_OKAY`. See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

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

1. **Branch first** — `git checkout master && git pull && git checkout -b <kebab-name>`. Never commit to `master`.
2. **Smallest change → build gate** — `/build-and-test` after every change; fix failures before anything else.
3. **Commit** — `/git-add` to stage, commit each logical unit. Use Conventional message style. Fix issues found by pre-commit.
4. **Review** — `/review` to spawn parallel sub-agents (architecture, security, C quality, embedded, tests) reviewing the patch from different angles.
5. **PR** — `/open-pr` to push, create PR, watch CI, and fix failures until green.

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
| `fake_sensors` | `CONFIG_FAKE_SENSORS` | DT-driven fake drivers (temp, humidity, CO2, VOC). Subscribes trigger chan, publishes event chan. Shell: `fake_sensors list/set/trigger`. Auto-publish timer at `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS`. |
| `sensor_registry` | `CONFIG_SENSOR_REGISTRY` | Runtime uid → metadata map. Sensors self-register at boot. User metadata via `CONFIG_SENSOR_REGISTRY_USER_META`: `sensor_registry_set_meta/get_meta/get_display_name/get_location`. Settings-persisted. |
| `remote_sensor` | `CONFIG_REMOTE_SENSOR` | Transport-agnostic abstraction for wireless sensors. Vtable pattern (`REMOTE_TRANSPORT_DEFINE()`), manager thread, UID derivation. Needs `remote_sensor_iterables.ld`. Shell: `remote_sensor list/scan/pair/unpair`. |
| `fake_remote_sensor` | `CONFIG_FAKE_REMOTE_SENSOR` | Testing stub implementing `remote_transport` vtable (`REMOTE_TRANSPORT_PROTO_FAKE`). |

### Services

| Library | Kconfig | Role |
|---|---|---|
| `config_cmd` | `CONFIG_CONFIG_CMD` | Owns `config_cmd_chan`. Commands: `SET_TRIGGER_INTERVAL`, `SNTP_RESYNC`, `MQTT_SET_ENABLED`, `MQTT_SET_BROKER`, `MQTT_SET_AUTH`, `MQTT_SET_GATEWAY`. |
| `sntp_sync` | `CONFIG_SNTP_SYNC` | Wall-clock SNTP sync at boot + periodic. Dedicated thread. Subscribes config_cmd_chan for resync requests. |
| `location_registry` | `CONFIG_LOCATION_REGISTRY` | Runtime CRUD for named locations. `add/remove/exists/foreach`. Settings-persisted (`loc/`). Shell: `location add/remove/list`. |
| `clock_display` | `CONFIG_CLOCK_DISPLAY` | Logs HH:MM UTC every 60s via delayable work item. |
| `sensor_event_log` | `CONFIG_SENSOR_EVENT_LOG` | No public API. Self-registers via `SYS_INIT`. Logs every sensor event to console. |

### Output / connectivity

| Library | Kconfig | Role |
|---|---|---|
| `http_dashboard` | `CONFIG_HTTP_DASHBOARD` | Web dashboard on port 8080. Chart.js timeseries, config page, auth (session cookie + bearer token). Self-init at APPLICATION 97. POST `/api/config` publishes on `config_cmd_chan` — does NOT call other libraries directly. Spinlock + snapshot pattern for ring buffer. Linker: `http_dashboard_sections.ld`. |
| `lvgl_display` | `CONFIG_LVGL_DISPLAY` | SDL 320×240 window. Analog clock + sensor cards. Subscribes event chan. `lvgl_display_run()` blocks on main thread (known ADR-008 violation, tracked in backlog). |
| `mqtt_publisher` | `CONFIG_MQTT_PUBLISHER` | Subscribes event chan. Topic: `{gw}/{location}/{display_name}/{type}`. Settings under `config/mqtt/` (server, port, user, pass, gw). Passwords base64-encoded. Shell: `mqtt_pub status/set`. Use `zsock_pollfd`/`zsock_poll()`/`ZSOCK_POLLIN` — not POSIX variants. |
| `pipe_publisher` | `CONFIG_PIPE_PUBLISHER` | Writes `env_sensor_data` as length-prefixed protobuf to POSIX FIFO. Sensor-node side for integration testing. |
| `pipe_transport` | `CONFIG_PIPE_TRANSPORT` | Reads from POSIX FIFO, decodes protobuf, publishes to `sensor_event_chan`. Gateway side for integration testing. |

## Integration tests (`tests/integration`)

Pytest via Twister `harness: pytest`. Full gateway on `native_sim/native/64`. Build/run commands: see [`README.md`](README.md#testing).

| Surface | Harness class | Fixture |
|---|---|---|
| UART shell | `ShellHarness` | `shell_harness` |
| HTTP API (port 8080) | `HttpHarness` | `http_harness` |
| HTTP API (authenticated) | `HttpHarness` + bearer token | `authed_harness` — use for all `POST /api/config` tests when auth is enabled |
| MQTT (port 1883) | `MqttHarness` | `mqtt_harness` (auto-skips if no broker) |

- **Page Object Model:** tests call harness methods, never raw strings.
- **Markers:** `smoke`, `shell`, `http`, `mqtt`, `e2e`, `system`.
- **DUT scope = session:** one boot per suite; restore state after mutations.
- **ZEPHYR_BASE override required:** `ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister ...`
- **MQTT broker:** `mosquitto -p 1883 -d`; tests auto-skip if none running.

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
