# ADR-002 — zbus as the System-Wide Communication Fabric

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

The weather station firmware has multiple independent producers and consumers
of data that must communicate without direct coupling:

- **Producers:** fake sensor drivers, real sensor drivers (future), LoRa RX module, BLE RX (future)
- **Consumers:** LVGL display manager, MQTT publisher, flash storage, logging

Without a structured communication mechanism, modules would reference each
other directly. This creates:
- Tight coupling — adding a new consumer requires modifying producers
- Hard-to-test code — modules can't be tested in isolation
- Race conditions — shared state without synchronisation primitives
- No clear boundary between layers

The firmware must also be testable on `native_sim` without hardware, which
rules out hardware-specific IPC mechanisms. And the architecture should be
explainable to an AI coding agent that generates new modules — clear,
mechanical rules produce correct code.

---

## Decision

Use **Zephyr's zbus** (Zephyr Message Bus) as the system-wide publish/subscribe
fabric for all inter-module communication.

### Core rules

1. **One concern = one channel.** Sensor measurements flow on `sensor_event_chan`.
   Sampling triggers flow on `sensor_trigger_chan`. LoRa link diagnostics flow
   on `lora_link_chan`. Channels are never reused for different data types.

2. **Channels are declared in shared headers, defined in exactly one `.c` file.**
   `ZBUS_CHAN_DECLARE` goes in `include/common/`. `ZBUS_CHAN_DEFINE` goes in
   the library that *owns* the channel (e.g. `lib/sensor_event/src/sensor_event.c`).

3. **Producers never know who their consumers are.**
   A sensor driver calls `zbus_chan_pub()` and returns. It has no knowledge of
   whether the display, MQTT publisher, or flash storage is listening.

4. **Consumers choose their subscription model:**
   - **Listener** (synchronous callback in publisher's context) — for low-latency,
     non-blocking reactions (e.g. logging)
   - **Subscriber** (thread-based, queued) — for work that takes time
     (e.g. LVGL rendering, MQTT publish)

### Channel map

```
                    PRODUCERS                        CONSUMERS
                    ─────────                        ─────────

  [fake_temp_0] ──┐
  [fake_hum_0]  ──┤
  [fake_temp_1] ──┤──► sensor_event_chan ──────────► [display_manager]  (subscriber)
  [fake_hum_1]  ──┤         │                ├────► [mqtt_manager]      (subscriber)
  [lora_rx]     ──┘         │                └────► [flash_storage]     (subscriber, future)
                            │
                            └── (zbus mutex-protected,
                                 shared-memory, no copy
                                 for listeners)

  [periodic_timer] ──┐
  [button_B3]     ──┤──► sensor_trigger_chan ────► [fake_temp_0]   (listener)
  [mqtt_cmd]      ──┘                         ├──► [fake_hum_0]   (listener)
                                              ├──► [fake_temp_1]  (listener)
                                              └──► [fake_hum_1]   (listener)

  [lora_rx] ─────────────► lora_link_chan ────────► [logger]       (listener)
                                               └──► [display_rssi] (subscriber, future)
```

### Defining a channel (pattern)

```c
/* lib/sensor_event/src/sensor_event.c — owns the channel */
#include <zephyr/zbus/zbus.h>
#include <common/sensor_event.h>

ZBUS_CHAN_DEFINE(sensor_event_chan,          /* name */
    struct env_sensor_data,                 /* message type */
    NULL,                                   /* validator (none) */
    NULL,                                   /* user data */
    ZBUS_OBSERVERS_EMPTY,                   /* initial observers */
    ZBUS_MSG_INIT(0)                        /* initial value */
);
```

```c
/* include/common/sensor_event.h — shared declaration */
ZBUS_CHAN_DECLARE(sensor_event_chan);
```

### Publishing (producer pattern)

```c
struct env_sensor_data evt = {
    .sensor_uid   = MY_UID,
    .type         = SENSOR_TYPE_TEMPERATURE,
    .q31_value    = temperature_c_to_q31(21.5),
    .timestamp_ms = k_uptime_get(),
};
int rc = zbus_chan_pub(&sensor_event_chan, &evt, K_MSEC(100));
if (rc != 0) {
    LOG_WRN("sensor_event_chan publish failed: %d", rc);
}
```

### Subscribing (consumer pattern — thread-based)

```c
ZBUS_SUBSCRIBER_DEFINE(mqtt_sub, 8);   /* queue depth = 8 messages */

void mqtt_thread(void)
{
    const struct zbus_channel *chan;
    struct env_sensor_data data;

    while (!zbus_sub_wait(&mqtt_sub, &chan, K_FOREVER)) {
        zbus_chan_read(&sensor_event_chan, &data, K_MSEC(100));
        mqtt_publish_sensor_event(&data);
    }
}
/* Register at channel definition time or via SYS_INIT */
ZBUS_CHAN_ADD_OBS(sensor_event_chan, mqtt_sub, 3);
```

### Kconfig required

```ini
CONFIG_ZBUS=y
CONFIG_ZBUS_CHANNEL_NAME=y      # names visible in logs and shell
CONFIG_ZBUS_OBSERVER_NAME=y     # observer names visible in logs
CONFIG_ZBUS_MSG_SUBSCRIBER=y    # enable queue-based subscribers
CONFIG_ZBUS_RUNTIME_OBSERVERS=y # allow SYS_INIT registration
```

---

## Consequences

**Easier:**
- Adding a new consumer (e.g. a cloud forwarder) = define a subscriber,
  call `ZBUS_CHAN_ADD_OBS`. Zero changes to any producer.
- Unit testing a producer = publish to channel, assert subscriber received
  correct message. No mocking of downstream modules.
- Extending to new sensor types = same channel, new `enum sensor_type` value.
  All consumers already handle the channel; most ignore unknown types gracefully.

**Harder:**
- Debugging message flow requires understanding zbus observer ordering and
  mutex ownership. The `zbus shell` command helps: `zbus channels` and
  `zbus observers`.
- Subscriber queue depth must be tuned. Too small = missed messages under
  load. Guideline: depth ≥ max burst of sensor events in one trigger cycle.

**Constrained:**
- Message structs must be copyable (no pointers to heap). This is enforced
  by the flat `env_sensor_data` design (see ADR-003).
- Listeners run in the publisher's thread context — must not block or sleep.
  Heavy work must use subscriber threads.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Direct function calls between modules | Tight coupling; producer must know all consumers; untestable in isolation |
| Zephyr message queues (`k_msgq`) | Point-to-point only; fan-out to multiple consumers requires manual multiplexing |
| Zephyr POSIX mqueues | Not available on all Zephyr targets; less integrated with Zephyr thread model |
| Custom callback registry | Reinvents zbus poorly; no tooling, no shell introspection |
| MQTT internally (on-device) | Absurd overhead; designed for network transport not intra-MCU IPC |
