# ADR-004 — Trigger-Driven Sensor Sampling (no sensor manager)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Sensor data must be sampled periodically and optionally on demand (button press,
MQTT command). A naive implementation centralises this in a "sensor manager"
module that holds references to all sensor devices, polls them on a timer, and
publishes results:

```c
/* REJECTED PATTERN — sensor manager anti-pattern */
void sensor_manager_thread(void) {
    while (1) {
        k_sleep(K_SECONDS(30));
        bme280_read(&temp, &hum);       /* knows about bme280 */
        sht4x_read(&temp2, &hum2);      /* knows about sht4x */
        lora_sensor_read(&remote_data); /* knows about lora */
        zbus_chan_pub(&sensor_event_chan, ...);
    }
}
```

This pattern has several serious problems:

1. **Tight coupling.** Adding a new sensor requires modifying the sensor manager.
   There is no way to add a sensor without touching a central file.

2. **Not testable in isolation.** The sensor manager cannot be unit tested
   without instantiating all sensors it references.

3. **Sampling policy is hardcoded.** The 30-second interval lives inside the
   manager. A button press refresh, MQTT-commanded sample, or startup burst
   all require special-casing inside the manager.

4. **Violates the modularity goal.** The whole architectural point of this
   project is that apps should be composed purely via Kconfig. A sensor manager
   that enumerates sensors by name defeats that.

5. **LoRa conflict.** Remote sensor data arrives asynchronously via LoRa RX,
   not by polling. A polling-based manager cannot cleanly integrate async
   sources.

---

## Decision

Sensor sampling is **trigger-driven and decentralised**. There is no sensor
manager module.

### The trigger channel

A `sensor_trigger_chan` zbus channel carries lightweight `sensor_trigger_event`
messages. Any module that wants sensor data to be refreshed publishes a trigger.
Each sensor driver independently subscribes to this channel and responds by
sampling and publishing to `sensor_event_chan`.

```
TRIGGER SOURCES              TRIGGER CHANNEL          SENSOR LISTENERS
───────────────              ───────────────          ────────────────

[Periodic timer]  ──────────►                 ──────► [fake_temp_indoor]
                             sensor_trigger   ──────► [fake_hum_indoor]
[Button B3 press] ──────────►    _chan        ──────► [fake_temp_outdoor]
                                             ──────► [fake_hum_outdoor]
[MQTT command]    ──────────►                ──────► [bme280 driver]  (future)
                                             ──────► [lora_rx]        (passive, ignores trigger)
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

`target_uid = 0` is a broadcast — all sensors respond. A specific UID
triggers only one sensor (useful for MQTT "read sensor X" commands).

### Sensor driver pattern

Each sensor driver follows the same three-step pattern:

**Step 1 — Subscribe to trigger channel:**
```c
ZBUS_LISTENER_DEFINE(my_sensor_trigger_listener, my_sensor_on_trigger);
/* Called in publisher's thread — must be fast, no blocking */
static void my_sensor_on_trigger(const struct zbus_channel *chan)
{
    const struct sensor_trigger_event *t = zbus_chan_const_msg(chan);
    if (t->target_uid != 0 && t->target_uid != MY_UID) {
        return;  /* not for us */
    }
    my_sensor_do_sample();  /* non-blocking: schedule work item */
}
```

**Step 2 — Sample and encode:**
```c
static void my_sensor_do_sample(void)
{
    int32_t raw_milli_c = hw_read_temperature_milli_c();
    struct env_sensor_data evt = {
        .sensor_uid   = MY_UID,
        .type         = SENSOR_TYPE_TEMPERATURE,
        .q31_value    = temperature_c_x1000_to_q31(raw_milli_c),
        .timestamp_ms = k_uptime_get(),
    };
    zbus_chan_pub(&sensor_event_chan, &evt, K_MSEC(100));
}
```

**Step 3 — Register listener at init:**
```c
static int my_sensor_init(void)
{
    ZBUS_CHAN_ADD_OBS(sensor_trigger_chan, my_sensor_trigger_listener, 0);
    return 0;
}
SYS_INIT(my_sensor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
```

### Periodic timer (in app `main.c`)

The only "policy" code in `main.c` is starting the trigger timer:

```c
static void trigger_timer_fn(struct k_timer *t)
{
    struct sensor_trigger_event evt = {
        .source     = TRIGGER_SOURCE_TIMER,
        .target_uid = 0,
    };
    zbus_chan_pub(&sensor_trigger_chan, &evt, K_NO_WAIT);
}
K_TIMER_DEFINE(sensor_poll_timer, trigger_timer_fn, NULL);

int main(void)
{
    k_timer_start(&sensor_poll_timer,
                  K_SECONDS(CONFIG_SENSOR_POLL_INTERVAL_S),
                  K_SECONDS(CONFIG_SENSOR_POLL_INTERVAL_S));
    /* Startup trigger — read immediately on boot */
    struct sensor_trigger_event boot = { .source = TRIGGER_SOURCE_STARTUP };
    zbus_chan_pub(&sensor_trigger_chan, &boot, K_MSEC(100));

    k_sleep(K_FOREVER);
    return 0;
}
```

`CONFIG_SENSOR_POLL_INTERVAL_S` is defined in `lib/sensor_trigger/Kconfig`
with a default of 30 seconds. Apps override it in `prj.conf` only.

### Adding a new sensor

Adding a sensor requires **zero changes to any existing file**:

1. Write `lib/my_new_sensor/src/my_new_sensor.c` following the three-step pattern.
2. Write `lib/my_new_sensor/Kconfig` with `menuconfig MY_NEW_SENSOR`.
3. Write `lib/my_new_sensor/CMakeLists.txt` with `if(CONFIG_MY_NEW_SENSOR) zephyr_library() ...`.
4. Add to board overlay: `my_new_sensor@X { compatible = "...", sensor-uid = <0xNNNN>; }`.
5. Add to `prj.conf`: `CONFIG_MY_NEW_SENSOR=y`.

The new sensor driver subscribes to `sensor_trigger_chan` via `SYS_INIT` and
begins publishing to `sensor_event_chan`. All existing consumers see the new
events immediately.

---

## Consequences

**Easier:**
- Adding sensors is purely additive — modify only the new sensor's files.
- Each sensor driver is unit-testable in isolation: publish a trigger, assert event.
- The sampling interval is a Kconfig knob — changed without touching source.
- LoRa and BLE async sources integrate naturally — they publish to `sensor_event_chan`
  directly without needing to respond to triggers.

**Harder:**
- There is no single place to see "all sensors that are active" at compile time.
  The sensor list is implicit in the DT + Kconfig combination. The `fake_sensors list`
  shell command and `sensor_registry` provide runtime introspection.
- A trigger with `target_uid=0` wakes all sensors simultaneously. If many sensors
  are slow (I2C, blocking reads), they must use work items or threads to avoid
  blocking the trigger publisher's thread for too long.

**Constrained:**
- Sensor listeners must not block in the trigger callback. If the sensor read
  is blocking (e.g. I2C with `K_FOREVER` timeout), the read must be deferred to
  a work queue item or dedicated sensor thread.
- LoRa RX is passive — it does not respond to triggers (data arrives when the
  remote node transmits). This is correct by design.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Central sensor manager (polling loop) | Tight coupling; breaks Kconfig-only composition; untestable; can't handle async LoRa/BLE sources uniformly |
| Per-sensor timers (each driver has its own) | No coordinated sampling; can't trigger all sensors together on button press; sampling intervals drift independently |
| Zephyr sensor trigger API (`sensor_trigger_set`) | Designed for hardware GPIO interrupts from the sensor itself; not suitable for software-commanded samples |
| RTOS periodic thread per sensor | Thread stack overhead per sensor; synchronisation complexity; overkill for simple read-and-publish |
