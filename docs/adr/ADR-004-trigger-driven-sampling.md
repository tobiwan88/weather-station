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

`sensor_trigger_chan` carries lightweight `sensor_trigger_event` messages.
Any module that wants sensor data refreshed publishes a trigger. Each sensor
driver independently subscribes and responds. `target_uid = 0` is a broadcast
to all sensors; a non-zero UID targets one sensor specifically.

This decoupling means: trigger sources do not know which sensors exist; sensor
drivers do not know which consumers will receive their events; consumers do not
know which sensors or trigger sources are present. Adding any of the three never
requires modifying the others.

For the sensor driver implementation pattern (subscribe at init, filter and
defer on trigger, sample and publish), and the "do not" rules, see
[`docs/architecture/event-bus.md`](../architecture/event-bus.md).

---

## Consequences

**Easier:**
- Adding sensors is purely additive — only the new sensor's files change.
- Each sensor driver is unit-testable in isolation: publish a trigger, assert event.
- LoRa and BLE async sources integrate naturally — they publish to `sensor_event_chan` without responding to triggers.

**Harder:**
- There is no single place to see all active sensors at compile time. The `fake_sensors list` shell command and `sensor_registry` provide runtime introspection.
- A broadcast trigger wakes all sensors simultaneously. Slow sensors (I2C) must use work items (enforced by the "do not block in the trigger callback" rule in [`docs/architecture/event-bus.md`](../architecture/event-bus.md)).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Central sensor manager (polling loop) | Tight coupling; breaks Kconfig-only composition (ADR-008); untestable; can't handle async LoRa/BLE sources uniformly |
| Zephyr sensor trigger API (`sensor_trigger_set`) | Designed for hardware GPIO interrupts from the sensor chip itself; not suitable for software-commanded samples or cross-sensor coordination |

---

## See also

- Current implementation: [`docs/architecture/event-bus.md`](../architecture/event-bus.md) (trigger-event split, ISR safety), [`docs/architecture/concurrency.md`](../architecture/concurrency.md) (execution contexts)
- Key types: `lib/sensor_trigger/include/sensor_trigger/sensor_trigger.h`
- Related ADRs: [ADR-002](ADR-002-zbus-as-system-bus.md) (zbus channels), [ADR-003](ADR-003-sensor-event-data-model.md) (event struct), [ADR-008](ADR-008-kconfig-app-composition.md) (no sensor manager rule)
