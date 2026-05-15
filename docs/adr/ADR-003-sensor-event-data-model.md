# ADR-003 — Sensor Event Data Model (flat struct + Q31)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Every sensor measurement must be represented as a message on `sensor_event_chan`. The message type must be scalable (new types without struct changes), flat (no pointers, safe to `memcpy`), granular (one event = one measurement), identifiable (carries source identity), and avoid floating-point on the data path.

---

## Decision

Use a **flat, single-measurement struct** with a **Q31 fixed-point value field** and a **`sensor_uid` for source identity**.

For the struct definition, `enum sensor_type` values, Q31 encode/decode helpers, and UID range allocation, see:
- `lib/sensor_event/include/sensor_event/sensor_event.h` — canonical type definitions
- `docs/architecture/event-bus.md` — message design rationale and ISR safety
- `docs/architecture/system-overview.md` — UID range allocation table

**One event = one measurement.** Temperature and humidity from the same physical sensor are two separate events. This keeps consumers simple and matches the wire format for remote sensors. Adding a new sensor type is additive — only `enum sensor_type` changes; the struct never does.

**Q31 fixed-point** avoids floating-point on the data path. Encode uses integer arithmetic only (safe in ISR and zbus thread contexts). Decode uses float and is acceptable only at the display/MQTT edge.

**`sensor_uid` is the stable identity key** for a physical measurement source. Consumers must never hardcode UIDs — they go through `sensor_registry`. A multi-channel sensor (e.g. BME280 with temperature and humidity) has one UID per channel.

**`env_sensor_data` is not a wire format.** When events must cross a device boundary, a dedicated encoding layer is required. No code outside a future `lib/sensor_codec` may serialise or deserialise `env_sensor_data` directly. Current MQTT payload formatting (`lib/mqtt_publisher`) and FIFO framing (`lib/pipe_transport`) are interim solutions. See backlog item `[SERIALIZATION]`.

---

## Consequences

**Easier:**
- New sensor types are additive — no breaking changes.
- Filtering by type or uid is a simple equality check.
- The fixed size makes queue depth calculations exact.

**Harder:**
- Consumers that need to correlate temperature + humidity must buffer and match by uid or timestamp.
- The Q31 range-per-type contract must be respected.

**Constrained:**
- Only scalar measurements fit this model. Vector/composite types need a different channel.
- No heap pointers — the struct is copied by value through zbus.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Nested struct with all fields | Struct grows with every new type; consumers must know all fields |
| `union` over all value types | Union size = largest member; type safety lost |
| Floating-point value field | Unsafe in ISR/callback context; varies across architectures |
| String-based representation | Expensive to encode/decode; no type safety; large messages |

---

## See also

- Current implementation: `docs/architecture/event-bus.md` (message design, ISR safety, channel ownership)
- Key types: `lib/sensor_event/include/sensor_event/sensor_event.h`
- Related ADRs: [ADR-002](ADR-002-zbus-as-system-bus.md) (channel ownership), [ADR-004](ADR-004-trigger-driven-sampling.md) (trigger pattern)
