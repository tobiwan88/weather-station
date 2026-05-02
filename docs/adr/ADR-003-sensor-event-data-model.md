# ADR-003 — Sensor Event Data Model (flat struct + Q31)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Every sensor measurement must be represented as a message on `sensor_event_chan`. The message type must be scalable (new types without struct changes), flat (no pointers, safe to `memcpy`), granular (one event = one measurement), identifiable (carries source identity), and avoid floating-point on the data path.

For the full struct definition, enum values, and Q31 encode/decode helpers, see `lib/sensor_event/include/sensor_event/sensor_event.h`.

---

## Decision

Use a **flat, single-measurement struct** with a **Q31 fixed-point value field** and a **`sensor_uid` for source identity**.

### Struct

```c
struct env_sensor_data {
    uint32_t         sensor_uid;
    enum sensor_type type;
    int32_t          q31_value;
    int64_t          timestamp_ms;
};
```

Size: 20 bytes on 32-bit, 24 bytes on 64-bit (padding before `int64_t`). A `BUILD_ASSERT` enforces `sizeof <= 32` at compile time.

### Sensor types (current)

| Enum | Physical range | Q31 encode |
|---|---|---|
| `SENSOR_TYPE_TEMPERATURE` | -40 .. +85 °C | `(temp_c + 40) / 125 * INT32_MAX` |
| `SENSOR_TYPE_HUMIDITY` | 0 .. 100 %RH | `humidity / 100 * INT32_MAX` |
| `SENSOR_TYPE_PRESSURE` | 300 .. 1100 hPa | *(encode helpers not yet implemented)* |
| `SENSOR_TYPE_CO2` | 0 .. 5000 ppm | `co2_ppm / 5000 * INT32_MAX` |
| `SENSOR_TYPE_VOC` | 0 .. 500 IAQ | `voc_iaq / 500 * INT32_MAX` |
| `SENSOR_TYPE_LIGHT` | 0 .. 100k lux | *(to be added)* |
| `SENSOR_TYPE_UV_INDEX` | 0 .. 11 | *(to be added)* |
| `SENSOR_TYPE_BATTERY_MV` | 0 .. 4200 mV | *(to be added)* |

New types: add to `enum sensor_type` only. The struct never changes.

### The `sensor_uid` contract

`sensor_uid` is the stable identity of a *physical measurement source*. A BME280 with temperature and humidity channels has **two** UIDs. Consumers must never hardcode UIDs — they go through `sensor_registry`.

| Range | Purpose |
|---|---|
| `0x0001–0x0FFF` | Local sensors (DT-assigned) |
| `0x1000–0xFFFF` | Remote sensors (generated at first registration, persisted) |

**Deferred:** `sensor_registry_get_scaling(uid)` for per-UID Q31 decode parameters. Currently consumers use type-specific inline helpers (`q31_to_temperature_c()`, etc.). See backlog item `[SENSOR-REGISTRY-SCALING]`.

### Q31 encoding

Q31 represents a signed value in [-1.0, +1.0) as a 32-bit integer. Encode uses **integer arithmetic only** (safe in any context). Decode uses float and is acceptable only in display/MQTT code.

### One event = one measurement

Temperature and humidity from the same physical sensor are two separate events. This keeps consumers simple and matches the wire format for remote sensors.

### Serialisation

`env_sensor_data` is **not a wire format**. When events must cross a device boundary (LoRa, MQTT, BLE, USB), a dedicated encoding layer is required. The format has not been chosen yet. See backlog item `[SERIALIZATION]`.

**Rule:** No code outside a future `lib/sensor_codec` may serialise or deserialise `env_sensor_data` directly. Currently, MQTT payload formatting is done in `lib/mqtt_publisher` and FIFO framing in `lib/pipe_transport` — these are interim solutions until a codec layer exists.

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
