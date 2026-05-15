# Event Bus Design

> Design rationale: [ADR-002](../adr/ADR-002-zbus-as-system-bus.md), [ADR-003](../adr/ADR-003-sensor-event-data-model.md), [ADR-004](../adr/ADR-004-trigger-driven-sampling.md).

## Channels

The system uses four zbus channels, each with a single owner and a distinct role:

| Channel | Owner | Direction |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | trigger sources → sensor drivers |
| `sensor_event_chan` | `lib/sensor_event` | sensor drivers → consumers |
| `config_cmd_chan` | `lib/config_cmd` | config producers (HTTP) → consumers (fake_sensors, sntp_sync, mqtt_publisher) |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | manager / shell → transport adapters |
| `remote_discovery_chan` | `lib/remote_sensor` | transport adapters → manager |

```mermaid
--8<-- "zbus-channels.mmd"
```

Remote sensor discovery uses `remote_discovery_chan` (zbus). Transport adapters call `remote_sensor_announce_disc()` which publishes to the channel. The manager dispatches discovery events to a workqueue via a zbus listener.

For the sensor-node ↔ gateway FIFO communication path, `pipe_publisher` writes length-prefixed protobuf frames to a POSIX FIFO and `pipe_transport` reads them back, publishing decoded events to `sensor_event_chan`. This path bypasses zbus entirely for cross-process communication.

The core sensor pipeline uses two channels. Separating trigger from event solves the question of **who decides when to sample** without coupling any component:

- `sensor_trigger_chan` — *when* to sample. Any code that wants sensors to fire publishes here. Sensor drivers don't know or care who triggered them.
- `sensor_event_chan` — *what was measured*. Any code that wants sensor readings subscribes here. Trigger sources don't know or care what consumes the data.

`config_cmd_chan` applies the same pattern to configuration: `http_dashboard` publishes a `config_cmd_event` when the user changes settings; `fake_sensors`, `sntp_sync`, and `mqtt_publisher` subscribe independently. Neither module references the other.

`remote_scan_ctrl_chan` follows the same pattern for the remote sensor layer — the manager and shell exchange scan control events without direct calls to transport adapters.

> **Note — k_msgq inside `remote_sensor_manager`:** The manager subscribes to
> `remote_scan_ctrl_chan` via a zbus listener, but internally bridges incoming
> events through a `k_msgq` so the listener callback returns immediately. The
> manager's dedicated thread then dequeues and processes events — including
> blocking operations such as peer registration and settings I/O — without
> stalling the zbus listener chain. This is the correct pattern when a single
> consumer needs sequential, potentially-blocking processing of channel events.

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

```mermaid
--8<-- "data-flow.mmd"
```

`target_uid = 0` in a trigger event is a broadcast — all sensors sample. A non-zero UID targets a single sensor, enabling on-demand sampling of one sensor without disturbing others.

Remote sensors (BLE, LoRa, Thread) publish on `sensor_event_chan` via `remote_sensor_publish_data()` — identical to local sensors from the perspective of all consumers.

---

## ISR Safety

The fake_sensors timer callback runs in ISR context. From there it publishes to `sensor_trigger_chan` with `K_NO_WAIT`. The zbus listener callbacks that fire from this — the sensor drivers — run in the zbus thread, not the ISR, so they can do normal work. The ISR only enqueues; it does not execute sensor logic.

This matters for the http_dashboard listener on `sensor_event_chan`. A timer → trigger → sensor → event chain means the dashboard's listener is ultimately triggered by a timer ISR. The listener appends to a ring buffer protected by a `k_spinlock` (not `k_mutex`) because spinlocks are the only synchronisation primitive that is safe to acquire from both thread and ISR-derived contexts.

---

## Sensor Driver Pattern

Every sensor driver follows the same three steps to integrate with the trigger–event channels.

**Step 1 — Subscribe at init:**
```c
ZBUS_LISTENER_DEFINE(my_sensor_listener, my_sensor_on_trigger);

static int my_sensor_init(void)
{
    ZBUS_CHAN_ADD_OBS(sensor_trigger_chan, my_sensor_listener, 0);
    return 0;
}
SYS_INIT(my_sensor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
```

**Step 2 — Filter and defer on trigger:**
```c
/* Called in publisher's thread — must not block */
static void my_sensor_on_trigger(const struct zbus_channel *chan)
{
    const struct sensor_trigger_event *t = zbus_chan_const_msg(chan);
    if (t->target_uid != 0 && t->target_uid != MY_UID) {
        return;
    }
    k_work_submit(&my_sensor_work);  /* defer blocking read off this thread */
}
```

> **Note:** The current fake sensor drivers publish directly in the trigger callback without `k_work` deferral. This is acceptable because fake sensors read from memory (no blocking I2C/SPI). Real hardware drivers **must** defer to `k_work` as shown above.

**Step 3 — Sample and publish:**
```c
static void my_sensor_work_fn(struct k_work *w)
{
    int32_t raw = hw_read_milli_c();
    struct env_sensor_data evt = {
        .sensor_uid   = MY_UID,
        .type         = SENSOR_TYPE_TEMPERATURE,
        .q31_value    = temperature_c_x1000_to_q31(raw),  /* see ADR-003 */
        .timestamp_ms = k_uptime_get(),
    };
    zbus_chan_pub(&sensor_event_chan, &evt, K_MSEC(100));
}
K_WORK_DEFINE(my_sensor_work, my_sensor_work_fn);
```

**Adding a new sensor** requires zero changes to any existing file:

1. Write `lib/my_sensor/` following the three-step pattern above.
2. Add to board overlay: `my_sensor@X { compatible = "...", sensor-uid = <0xNNNN>; }`.
3. Enable in `prj.conf`: `CONFIG_MY_SENSOR=y`.

**Do not:**
- **Do not create a sensor manager.** No module may hold references to multiple sensor devices and coordinate their reads.
- **Do not block in the trigger callback.** If the sensor read is blocking (I2C, SPI), defer it to a `k_work` item as shown above.
- **Do not give LoRa RX a trigger listener.** Remote sensor data arrives asynchronously; `lora_rx` publishes directly to `sensor_event_chan` when a packet arrives.
- **Do not start per-sensor timers.** The shared trigger channel is the only sampling clock — it allows coordinated reads (button press refreshes all sensors together).

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
