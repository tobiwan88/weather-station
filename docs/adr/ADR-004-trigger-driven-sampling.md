# ADR-004 — Trigger-Driven Sensor Sampling (no sensor manager)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

A naive implementation centralises sampling in a "sensor manager" thread that
holds references to all sensor devices and polls them on a timer. This breaks
Kconfig-only app composition (ADR-008): every new sensor requires modifying a
central file, making isolated testing impossible and async sources (LoRa RX)
impossible to integrate uniformly.

---

## Decision

Sensor sampling is **trigger-driven and decentralised**. There is no sensor
manager module.

### The trigger channel

`sensor_trigger_chan` carries lightweight `sensor_trigger_event` messages.
Any module that wants sensor data refreshed publishes a trigger. Each sensor
driver independently subscribes and responds.

```
TRIGGER SOURCES              TRIGGER CHANNEL          SENSOR LISTENERS
───────────────              ───────────────          ────────────────

[Periodic timer]  ──────────►                 ──────► [fake_temp_indoor]
                             sensor_trigger   ──────► [fake_hum_indoor]
[Button press]    ──────────►    _chan        ──────► [fake_temp_outdoor]
                                             ──────► [bme280 driver]  (future)
[MQTT command]    ──────────►                ──────► [lora_rx]  (passive — ignores)
[Startup SYS_INIT]──────────►

                                                        each listener publishes
                                                        to sensor_event_chan ↓

                             sensor_event_chan ──────► [display_manager]
                                              ──────► [mqtt_manager]
                                              ──────► [flash_storage]
```

### Trigger event struct

```c
struct sensor_trigger_event {
    enum trigger_source source;     /* who requested the sample */
    uint32_t            target_uid; /* 0 = broadcast to all sensors */
};
```

`target_uid = 0` is a broadcast. A specific UID triggers only one sensor.
For the UID contract see ADR-003.

### Sensor driver pattern

Every sensor driver follows the same three steps:

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

### Adding a new sensor

Zero changes to any existing file:

1. Write `lib/my_sensor/` following the three-step pattern above.
2. Add to board overlay: `my_sensor@X { compatible = "...", sensor-uid = <0xNNNN>; }`.
3. Enable in `prj.conf`: `CONFIG_MY_SENSOR=y`.

### Do not

- **Do not create a sensor manager.** No module may hold references to multiple sensor devices and coordinate their reads.
- **Do not block in the trigger callback.** If the sensor read is blocking (I2C, SPI), defer it to a `k_work` item as shown above.
- **Do not give LoRa RX a trigger listener.** Remote sensor data arrives asynchronously; `lora_rx` publishes directly to `sensor_event_chan` when a packet arrives.
- **Do not start per-sensor timers.** The shared trigger channel is the only sampling clock — it allows coordinated reads (button press refreshes all sensors together).

The periodic timer that publishes to `sensor_trigger_chan` lives in `lib/sensor_trigger/` and is configured via `CONFIG_SENSOR_POLL_INTERVAL_S`. See ADR-008 for the `main.c` rule.

---

## Consequences

**Easier:**
- Adding sensors is purely additive — only the new sensor's files change.
- Each sensor driver is unit-testable in isolation: publish a trigger, assert event.
- LoRa and BLE async sources integrate naturally — they publish to `sensor_event_chan` without responding to triggers.

**Harder:**
- There is no single place to see all active sensors at compile time. The `fake_sensors list` shell command and `sensor_registry` provide runtime introspection.
- A broadcast trigger wakes all sensors simultaneously. Slow sensors (I2C) must use work items (enforced by the "do not block" rule above).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Central sensor manager (polling loop) | Tight coupling; breaks Kconfig-only composition (ADR-008); untestable; can't handle async LoRa/BLE sources uniformly |
| Zephyr sensor trigger API (`sensor_trigger_set`) | Designed for hardware GPIO interrupts from the sensor chip itself; not suitable for software-commanded samples or cross-sensor coordination |
