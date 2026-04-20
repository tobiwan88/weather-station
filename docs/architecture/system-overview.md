# System Overview

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
│  channels, never by calling each other.          │
├──────────────────────────────────────────────────┤
│  Zephyr RTOS                                     │
│  zbus · k_timer · k_work · HTTP_SERVER · LVGL   │
└──────────────────────────────────────────────────┘
```

---

## Libraries and Their Roles

| Library | Role |
|---|---|
| `sensor_event` | Owns the event data model (`env_sensor_data`) and the channel all sensors publish to |
| `sensor_trigger` | Owns the trigger event type and the channel all trigger sources publish to |
| `sensor_registry` | Runtime mapping of sensor UID → rich user-editable metadata; sensors self-register at boot |
| `fake_sensors` | Emulated local sensor drivers; subscribes to trigger channel, publishes to event channel |
| `sntp_sync` | Maintains wall-clock time via SNTP; provides a single authoritative `get_epoch_ms()` |
| `clock_display` | Reads wall-clock time every 60 s and logs it; depends on nothing else |
| `http_dashboard` | Web dashboard and config API; subscribes to event channel, publishes on config_cmd_chan |
| `lvgl_display` | LVGL render loop; runs on the main thread because SDL requires it |
| `config_cmd` | Owns the `config_cmd_chan` zbus channel; decouples config producers (HTTP) from consumers (fake_sensors, sntp_sync) |
| `location_registry` | Runtime CRUD for named physical locations; replaces compile-time DT location properties |
| `sensor_event_log` | Self-registering zbus listener that logs every sensor event to console; no public API |
| `mqtt_publisher` | Subscribes to `sensor_event_chan`; publishes readings to an MQTT broker under `{gw}/{location}/{name}/{type}` |
| `remote_sensor` | Transport-agnostic abstraction for wireless remote sensors; vtable pattern, manager thread, UID derivation |
| `fake_remote_sensor` | Simulated remote sensor transport adapter for testing; implements the `remote_transport` vtable |

The critical point: `http_dashboard` and `fake_sensors` do not reference each other. `http_dashboard` publishes a `config_cmd_event` on `config_cmd_chan`; `fake_sensors` subscribes independently. Neither knows the other exists.

---

## Key Design Rules

**One event per physical measurement.** Temperature and humidity from the same physical sensor are separate `env_sensor_data` events on the same channel. This keeps consumers simple (each reading is self-describing) and matches the wire format for LoRa (fixed 20-byte frame, no aggregation).

**`main.c` is wiring, not logic.** The only things permitted in `main.c` are `LOG_MODULE_REGISTER`, a `SYS_INIT` call for the gateway's own zbus listener, and either `lvgl_display_run()` or `k_sleep(K_FOREVER)`. All behaviour lives in libraries.

**No library-to-library calls.** Libraries are not allowed to `#include` each other's headers or call each other's functions. The only shared surface is zbus channels and the two structs in `include/common/`. If two libraries need to coordinate, they do so through a channel, not a function call.

**Q31 on the wire, float only at the edges.** Sensor values are stored and transmitted as Q31 fixed-point. Conversion to `float` happens only when formatting for human display (logs, HTTP JSON). This avoids floating-point in ISR contexts and keeps the wire format deterministic.

---

## Extension Points

| To add | What changes | What does not change |
|---|---|---|
| New sensor consumer (flash logger, display) | One new zbus listener on `sensor_event_chan` (this is how `mqtt_publisher` was added) | All existing consumers and sensor drivers |
| New trigger source (button, MQTT command) | Publish `sensor_trigger_event` to `sensor_trigger_chan` | All sensor drivers |
| Real hardware sensor (BME280, SHT4x) | New driver that subscribes to trigger chan, publishes to event chan | All consumers |
| New sensor type (pressure, CO₂) | New `enum sensor_type` value; existing consumers that don't handle it ignore it | All existing consumers |
