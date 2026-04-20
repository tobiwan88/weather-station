# Event Bus Design

## Channels

The system uses five zbus channels, each with a single owner and a distinct role:

| Channel | Owner | Direction |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | trigger sources → sensor drivers |
| `sensor_event_chan` | `lib/sensor_event` | sensor drivers → consumers |
| `config_cmd_chan` | `lib/config_cmd` | config producers (HTTP) → consumers (fake_sensors, sntp_sync) |
| `remote_discovery_chan` | `lib/remote_sensor` | transport adapters → remote_sensor_manager |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | manager / shell → transport adapters |

The core sensor pipeline uses two channels. Separating trigger from event solves the question of **who decides when to sample** without coupling any component:

- `sensor_trigger_chan` — *when* to sample. Any code that wants sensors to fire publishes here. Sensor drivers don't know or care who triggered them.
- `sensor_event_chan` — *what was measured*. Any code that wants sensor readings subscribes here. Trigger sources don't know or care what consumes the data.

`config_cmd_chan` applies the same pattern to configuration: `http_dashboard` publishes a `config_cmd_event` when the user changes settings; `fake_sensors` and `sntp_sync` subscribe independently. Neither module references the other.

`remote_discovery_chan` and `remote_scan_ctrl_chan` follow the same pattern for the remote sensor layer — transport adapters and the manager exchange events without direct calls.

---

## The Trigger–Event Split

```
trigger sources          sensor_trigger_chan       sensor drivers
──────────────           ───────────────────       ──────────────
timer (periodic)  ──►                        ──►  fake_temperature
startup (once)    ──►    { source, uid }      ──►  fake_humidity
button ISR        ──►                        ──►  remote_sensor (pull)
                                             ──►  (future: real hw)

sensor drivers           sensor_event_chan          consumers
──────────────           ────────────────           ──────────────
fake_temperature  ──►                        ──►  sensor_event_log
fake_humidity     ──►    { uid, type,         ──►  http_dashboard
remote_sensor     ──►      q31, timestamp }   ──►  mqtt_publisher
(future: real hw) ──►                        ──►  (future: flash)
```

`target_uid = 0` in a trigger event is a broadcast — all sensors sample. A non-zero UID targets a single sensor, enabling on-demand sampling of one sensor without disturbing others.

Remote sensors (BLE, LoRa, Thread) publish on `sensor_event_chan` via `remote_sensor_publish_data()` — identical to local sensors from the perspective of all consumers.

---

## ISR Safety

The fake_sensors timer callback runs in ISR context. From there it publishes to `sensor_trigger_chan` with `K_NO_WAIT`. The zbus listener callbacks that fire from this — the sensor drivers — run in the zbus thread, not the ISR, so they can do normal work. The ISR only enqueues; it does not execute sensor logic.

This matters for the http_dashboard listener on `sensor_event_chan`. A timer → trigger → sensor → event chain means the dashboard's listener is ultimately triggered by a timer ISR. The listener appends to a ring buffer protected by a `k_spinlock` (not `k_mutex`) because spinlocks are the only synchronisation primitive that is safe to acquire from both thread and ISR-derived contexts.

---

## Open/Closed in Practice

The principle: adding a new consumer must not require modifying existing producers.

Concretely, when `http_dashboard` was added to the project, the following files were **not touched**: `fake_temperature.c`, `fake_humidity.c`, `fake_sensors_timer.c`, `apps/gateway/src/main.c`. The dashboard simply registered a listener on `sensor_event_chan` in its own `SYS_INIT` callback.

The same was true when `mqtt_publisher` was added, and will continue to be true for the next consumer (flash logger, another display widget). The channel is the only shared contract.

---

## Message Design

`env_sensor_data` is 20 bytes on a 32-bit target. The constraints driving its size:

- **LoRa MTU** — a LoRa packet at SF12/125 kHz has a practical payload limit of ~51 bytes. A single event frame must fit with room for a header.
- **No heap** — the struct is copied by value through zbus. No pointers, no dynamic allocation.
- **Self-describing** — `sensor_uid` and `type` are included in every event so any consumer can interpret the reading without external context.
- **Q31 fixed-point** — avoids floating-point in ISR and zbus thread contexts. The Q31 range covers ±1.0; temperature and humidity are scaled so that 0.0–1.0 maps to 0–100 (humidity) or -40–85 °C (temperature), staying within the fixed-point range.

A `BUILD_ASSERT` enforces `sizeof(env_sensor_data) <= 32` at compile time to catch accidental growth.
