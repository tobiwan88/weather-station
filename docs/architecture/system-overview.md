# System Overview

> Design rationale: [ADR-001](../adr/ADR-001-repo-and-workspace-structure.md), [ADR-007](../adr/ADR-007-gateway-display-combined.md), [ADR-008](../adr/ADR-008-kconfig-app-composition.md), [ADR-009](../adr/ADR-009-native-sim-first.md).

## Goals

The gateway is a coordinator node that aggregates sensor readings, displays them locally, exposes them over HTTP, and keeps wall-clock time via SNTP. The design prioritises two properties above all else:

1. **Adding a new consumer of sensor data must not require touching any existing code.** The gateway listener, the HTTP dashboard, and the MQTT publisher are all independent — none knows the others exist.
2. **Feature selection must be possible without editing C code.** Whether the HTTP dashboard or LVGL display is included is a `prj.conf` decision, not a `main.c` decision.

These two goals drive every structural choice in the codebase.

---

## Layers

```
┌──────────────────────────────────────────────────┐
│  Application  apps/gateway/  apps/sensor-node/   │
│  ─────────────────────────────────────────────── │
│  main.c declares nothing; all features via       │
│  SYS_INIT auto-wiring and prj.conf selection     │
├──────────────────────────────────────────────────┤
│  Libraries    lib/                               │
│  ─────────────────────────────────────────────── │
│  Independently optional libraries,               │
│  each compiled only when its CONFIG_ is set.     │
│  Libraries communicate only through zbus         │
│  channels, never by calling each other's         │
│  internal logic. Public read-only APIs           │
│  (sensor_registry, location_registry) may be     │
│  called by any library.                          │
├──────────────────────────────────────────────────┤
│  Zephyr RTOS                                     │
│  zbus · k_timer · k_work · HTTP_SERVER · LVGL   │
└──────────────────────────────────────────────────┘
```

```mermaid
--8<-- "system-overview.mmd"
```

---

## Libraries and Their Roles

| Library | Role |
|---|---|
| `sensor_event` | Owns the event data model (`env_sensor_data`) and the channel all sensors publish to |
| `sensor_trigger` | Owns the trigger event type and the channel all trigger sources publish to |
| `sensor_registry` | Runtime mapping of sensor UID → rich user-editable metadata; sensors self-register at boot |
| `fake_sensors` | Emulated local sensor drivers (temp, humidity, CO2, VOC); subscribes to trigger channel, publishes to event channel |
| `sntp_sync` | Maintains wall-clock time via SNTP; provides a single authoritative `get_epoch_ms()` |
| `clock_display` | Reads wall-clock time every 60 s and logs it; depends on nothing else |
| `http_dashboard` | Web dashboard and config API; subscribes to event channel, publishes on config_cmd_chan |
| `lvgl_display` | LVGL render loop; runs on the main thread because SDL requires it |
| `config_cmd` | Owns the `config_cmd_chan` zbus channel; decouples config producers (HTTP) from consumers (fake_sensors, sntp_sync, mqtt_publisher) |
| `location_registry` | Runtime CRUD for named physical locations; replaces compile-time DT location properties |
| `sensor_event_log` | Self-registering zbus listener that logs every sensor event to console; no public API |
| `mqtt_publisher` | Subscribes to `sensor_event_chan`; publishes readings to an MQTT broker under `{gw}/{location}/{name}/{type}` |
| `remote_sensor` | Transport-agnostic abstraction for wireless remote sensors; vtable pattern, manager thread, UID derivation |
| `fake_remote_sensor` | Simulated remote sensor transport adapter for testing; implements the `remote_transport` vtable |
| `pipe_publisher` | Writes `env_sensor_data` events as length-prefixed protobuf to a POSIX FIFO (sensor-node side) |
| `pipe_transport` | Reads from POSIX FIFO, decodes protobuf frames, publishes to `sensor_event_chan` (gateway side) |
| `sensor_node_tx` | Subscribes to `sensor_event_chan`, accumulates readings in a ring buffer, transmits batched 5-byte LoRa frames on timer or force-TX command; shell: `sensor_node_tx force/enable/disable/status` |
| `trace_recorder` | Overrides Zephyr tracing hooks to capture thread switch/ISR/idle events in a static ring buffer for Renode post-mortem dump |
| `uart_lora_bridge` | Receives 24-byte wire frames from LoRa co-processor over UART, converts to `env_sensor_data`, publishes to `sensor_event_chan` (gateway side) |
| `uart_lora_sender` | Subscribes to `sensor_event_chan`, packs events into 24-byte wire frames, sends over UART to gateway (co-processor side) |
| `lora_radio` | LoRa bounded context: radio driver, packet framing, session management, protocol handlers; runs on STM32WLE5JC co-processor |

```mermaid
--8<-- "library-deps.mmd"
```

The critical point: `http_dashboard` and `fake_sensors` do not reference each other. `http_dashboard` publishes a `config_cmd_event` on `config_cmd_chan`; `fake_sensors` subscribes independently. Neither knows the other exists.

---

## Key Design Rules

**One event per physical measurement.** Temperature and humidity from the same physical sensor are separate `env_sensor_data` events on the same channel. This keeps consumers simple (each reading is self-describing) and matches the wire format for LoRa (fixed 20-byte frame, no aggregation).

**`main.c` is wiring, not logic.** The only things permitted in `main.c` are `LOG_MODULE_REGISTER`, a `SYS_INIT` call for the gateway's own zbus listener, and either `lvgl_display_run()` or `k_sleep(K_FOREVER)`. All behaviour lives in libraries.

**No library-to-library calls for internal logic.** Libraries are not allowed to `#include` each other's internal headers or call each other's private functions. The only shared surface for coordination is zbus channels. Two exceptions exist:
- **`sensor_registry`** and **`location_registry`** expose public read-only APIs that any library may call (e.g. `http_dashboard` reads display names and locations for JSON responses).
- **Integration files** compiled conditionally when two CONFIG symbols are both set (e.g. `fake_sensors_config_cmd.c` compiled when both `CONFIG_FAKE_SENSORS` and `CONFIG_CONFIG_CMD` are enabled).

If two libraries need to coordinate at runtime, they do so through a channel, not a function call.

**Q31 on the wire, float only at the edges.** Sensor values are stored and transmitted as Q31 fixed-point. Conversion to `float` happens only when formatting for human display (logs, HTTP JSON). This avoids floating-point in ISR contexts and keeps the wire format deterministic.

---

## Sensor UID Allocation

`sensor_uid` is the runtime identity key used by `sensor_registry`, LVGL cards, and
MQTT topics. UIDs are assigned at DT node definition time (in `apps/<app>/boards/native_sim.overlay`)
and must be unique across all overlay files in the project.

| Range | Purpose |
|---|---|
| `0x0001–0x000F` | Gateway-local / indoor sensors |
| `0x0011–0x001F` | Gateway outdoor sensors |
| `0x0021–0x00FF` | Remote sensor nodes (LoRa, BLE, etc.) |
| `0x0101+` | Test-only instances |

Use the lowest free UID in the appropriate range. Never reuse a UID — UIDs are
the identity key for `sensor_registry`, LVGL display cards, and MQTT topic paths.

---

## Repository and Workspace Structure

The repo uses Zephyr **T2 topology** (application-as-manifest): `weather-station` is simultaneously the west manifest repository *and* a Zephyr module. `west init -l .` points west at the local manifest; `west update` fetches Zephyr and external modules.

```
weather-station/               ← git repo root, also west manifest
│
├── west.yml                   ← declares Zephyr version + module allowlist
├── zephyr/module.yml          ← registers repo as a Zephyr module
├── CMakeLists.txt             ← module-level: add_subdirectory lib drivers
├── Kconfig                    ← module-level: rsource sub-Kconfigs
├── VERSION                    ← semantic version (MAJOR.MINOR.PATCHLEVEL)
│
├── apps/                      ← one sub-directory per firmware image
│   ├── gateway/               ← Wi-Fi hub + LVGL display
│   ├── sensor-node/           ← LoRa TX beacon
│   └── lora_bridge/           ← Wio-E5 Mini LoRa co-processor firmware
│
├── lib/                       ← shared reusable libraries (west modules)
│   ├── sensor_event/          ← env_sensor_data struct, Q31 helpers, zbus channel
│   ├── sensor_trigger/        ← sensor_trigger_event struct, zbus channel
│   ├── sensor_registry/       ← uid → label/location/scaling metadata
│   ├── fake_sensors/          ← DT-instantiated fake drivers + auto-publish timer
│   ├── sntp_sync/             ← SNTP time sync with runtime resync
│   ├── clock_display/         ← wall-clock widget for LVGL display
│   ├── lvgl_display/          ← LVGL display manager (sensor tiles)
│   ├── http_dashboard/        ← Chart.js timeseries + config REST API
│   ├── uart_lora_bridge/      ← gateway-side UART receiver (24-byte wire → zbus)
│   ├── uart_lora_sender/      ← co-processor-side UART sender (zbus → 24-byte wire)
│   └── lora_radio/            ← LoRa bounded context (SX126x, sessions, FOTA)
│
├── include/common/            ← shared headers (zbus channel declarations,
│                                 data structs, Q31 helpers)
│
├── drivers/                   ← out-of-tree Zephyr drivers (future real HW)
├── dts/bindings/              ← custom devicetree bindings (fake,temperature…)
├── boards/                    ← custom board definitions
│   └── seeed/wio_e5_mini/     ← Wio-E5 Mini (STM32WLE5JC) board definition
│
├── tests/                     ← twister test suites
├── simulation/                ← Renode .resc and Robot Framework scripts
│
├── .devcontainer/             ← VS Code devcontainer (tobiwan88/zephyr_docker)
├── .github/workflows/         ← CI (build + twister + Renode)
├── scripts/
│   ├── parse-trace.py         ← Binary trace → Perfetto JSON post-processor
│   └── trace-and-analyze      ← One-command Renode + trace dev workflow
```

The `west.yml` uses a `name-allowlist` import to fetch only the Zephyr modules this project needs. Without it, west would clone every Zephyr module (~30+), most of which this project never uses.

Apps never reference `lib/` via CMake paths. Instead:
1. `zephyr/module.yml` tells Zephyr the repo root is a module.
2. The root `CMakeLists.txt` calls `add_subdirectory(lib)`.
3. Each `lib/*/CMakeLists.txt` calls `zephyr_library()` (conditional on Kconfig).
4. Apps enable libraries via `prj.conf` Kconfig symbols only.

---

## Gateway and Display Architecture

For v1, the `apps/gateway/` firmware image contains both the gateway logic (Wi-Fi, MQTT, HTTP, LoRa RX) and the display logic (LVGL, button handler). They run in the same Zephyr image on the same MCU.

Even though gateway and display are in one image, they are **architecturally separate modules** communicating only via zbus. The display manager never calls MQTT functions. The MQTT publisher never calls LVGL functions.

```
                    apps/gateway (single firmware image)
┌───────────────────────────────────────────────────────────────┐
│                                                               │
│  SENSOR PRODUCERS              SHARED BUS     CONSUMERS       │
│  ─────────────────             ──────────     ─────────       │
│                                                               │
│  [fake_temp_indoor] ──┐                                       │
│  [fake_hum_indoor]  ──┤                  ┌──► [display_mgr]  │
│  [fake_temp_outdoor]──┤─► sensor_event ──┤    (LVGL thread)  │
│  [fake_hum_outdoor] ──┤       _chan      ├──► [mqtt_manager] │
│  [lora_rx thread]   ──┘                  └──► [flash_storage]│
│                                               (future)        │
│                                                               │
│  [periodic timer] ──┐                                         │
│  [button B3]      ──┤─► sensor_trigger_chan ──► all sensors   │
│  [mqtt command]   ──┘                                         │
│                                                               │
│  Wi-Fi ──► HTTP server  (config page, port 8080)              │
│        ──► MQTT client  (→ Mosquitto)                         │
│                                                               │
│  Display hardware ──► LVGL ──► display_manager subscriber     │
│  Button B1-B4     ──► button handler ──► sensor_trigger_chan  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

Button responsibilities:

| Button | zbus action |
|--------|------------|
| B1 | Previous screen (display-internal) |
| B2 | Next screen (display-internal) |
| B3 | Publish `sensor_trigger_event` with `TRIGGER_SOURCE_BUTTON` |
| B4 | Settings / backlight (display-internal) |

On `native_sim`, buttons are simulated by the shell: `fake_sensors trigger` is equivalent to a B3 press.

### Display routing via sensor_registry

The display manager never hardcodes which UID maps to which tile. It looks up `sensor_registry_lookup(uid)->location` and routes values accordingly. Adding a new room (e.g. "garage") requires adding a new tile to the LVGL layout — no change to the routing logic.

### Future split path

Because gateway and display communicate **only via zbus**, splitting them into separate devices requires only configuration changes — no source code changes to any library:

| Phase 1 (current) | Phase 2 (future) |
|---|---|
| `apps/gateway/` with `DISPLAY=y LVGL=y` | `apps/gateway/` with `DISPLAY=n` + `apps/display-unit/` with `DISPLAY=y` |
| Single build target | Two build targets, connected by UART bridge (serialised zbus events) |

---

## Extension Points

| To add | What changes | What does not change |
|---|---|---|
| New sensor consumer (flash logger, display) | One new zbus listener on `sensor_event_chan` (this is how `mqtt_publisher` was added) | All existing consumers and sensor drivers |
| New trigger source (button, MQTT command) | Publish `sensor_trigger_event` to `sensor_trigger_chan` | All sensor drivers |
| Real hardware sensor (BME280, SHT4x) | New driver that subscribes to trigger chan, publishes to event chan | All consumers |
| New sensor type (pressure, CO₂, VOC) | New `enum sensor_type` value; existing consumers that don't handle it ignore it | All existing consumers |
| New transport adapter (BLE, Thread) | Implement `remote_transport` vtable; register with `remote_sensor` | All consumers, manager |
