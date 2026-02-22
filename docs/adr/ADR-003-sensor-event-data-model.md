# ADR-003 — Sensor Event Data Model (flat struct + Q31)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Every sensor measurement in the system — whether from a local I2C sensor, a
LoRa-received packet, or a shell-injected fake value — must be represented
as a message on `sensor_event_chan`. The message type must satisfy several
competing constraints:

- **Scalable:** Adding a new sensor type (CO2, UV index, battery voltage…)
  must not change the struct size or ABI. Existing consumers must keep
  working without modification.
- **Flat:** zbus messages are copied by value. No pointers, no heap.
  The struct must be safe to `memcpy`.
- **Granular:** One struct = one physical measurement. Temperature and humidity
  from the same BME280 chip are *two separate events* on the channel. This
  allows consumers to subscribe to specific types and ignore others without
  filtering overhead.
- **Identifiable:** Each event must carry a stable identity for its source so
  the display can route "outdoor temperature" to the outdoor panel and
  "indoor humidity" to the indoor panel — without hardcoding locations in
  consumer code.
- **No floating point on the data path.** MCUs without FPU, sensor drivers,
  and ISR-adjacent code must not use `float` or `double`. Conversion to
  human-readable values happens only at the boundary (display rendering,
  MQTT JSON serialisation).

An early design used a nested struct with named fields per sensor type:
```c
/* REJECTED — does not scale */
struct env_sensor_data {
    int32_t temperature_milli_c;
    int32_t humidity_milli_pct;
    int32_t pressure_pa;
    int64_t timestamp_ms;
};
```
This struct "explodes" with every new sensor type, forces consumers to handle
all fields even when they care about only one, and bundles multiple measurements
into one event making granular subscription impossible.

---

## Decision

Use a **flat, single-measurement struct** with a **Q31 fixed-point value field**
and a **`sensor_uid` for source identity**.

### Struct definition

```c
/* include/common/sensor_event.h */

enum sensor_type {
    SENSOR_TYPE_TEMPERATURE = 0,  /* Q31, physical range: -40 .. +85 °C    */
    SENSOR_TYPE_HUMIDITY    = 1,  /* Q31, physical range:   0 .. 100 %RH   */
    SENSOR_TYPE_PRESSURE    = 2,  /* Q31, physical range: 300 .. 1100 hPa  */
    SENSOR_TYPE_CO2         = 3,  /* Q31, physical range: 400 .. 5000 ppm  */
    SENSOR_TYPE_LIGHT       = 4,  /* Q31, physical range:   0 .. 100k lux  */
    SENSOR_TYPE_UV_INDEX    = 5,  /* Q31, physical range:   0 .. 11        */
    SENSOR_TYPE_BATTERY_MV  = 6,  /* Q31, physical range:   0 .. 4200 mV   */
    /* New types: add here only. Struct never changes. */
};

struct env_sensor_data {
    uint32_t         sensor_uid;    /* Stable DT-assigned ID, unique per
                                       physical measurement source           */
    enum sensor_type type;          /* What physical quantity this event is  */
    int32_t          q31_value;     /* Q31 fixed-point encoding (see below)  */
    int64_t          timestamp_ms;  /* k_uptime_get() at sample time         */
};
```

Size: **16 bytes** — fits comfortably in a zbus message, a LoRa packet sample,
and a flash log record.

### The `sensor_uid` contract

`sensor_uid` is assigned in devicetree and never changes across firmware
updates or reboots. It is the stable identity of a *physical measurement source*
— not a device. A BME280 with both temperature and humidity channels has
**two** sensor UIDs.

```dts
/* native_sim.overlay */
fake_temp_indoor: fake_temperature@0 {
    sensor-uid = <0x0001>;   /* ← assigned here, stable forever */
    location = "living_room";
};
fake_hum_indoor: fake_humidity@0 {
    sensor-uid = <0x0002>;   /* ← different UID, same physical chip */
    location = "living_room";
};
```

The `sensor_registry` library maps UIDs to metadata (location string, label)
at runtime. Display and MQTT code look up metadata by UID — they never
hardcode locations.

### Q31 encoding

Q31 represents a signed value in the range **[-1.0, +1.0)** as a 32-bit
integer. Each sensor type maps its physical range onto this interval:

```
                   Physical range        Q31 encoding
                   ──────────────        ────────────
TEMPERATURE:   [-40°C .. +85°C]   →   [-1.0 .. +1.0)
  Formula:  q31 = (temp_c - (-40)) / 125 * INT32_MAX
  Inverse:  temp_c = q31 / INT32_MAX * 125 + (-40)

HUMIDITY:       [0%RH .. 100%RH]  →   [0.0 .. +1.0)
  Formula:  q31 = humidity / 100 * INT32_MAX
  Inverse:  humidity = q31 / INT32_MAX * 100

PRESSURE:   [300hPa .. 1100hPa]   →   [-1.0 .. +1.0)
  Formula:  q31 = (press_hpa - 700) / 400 * INT32_MAX
  Inverse:  press_hpa = q31 / INT32_MAX * 400 + 700
```

The encode/decode helpers in `include/common/q31.h` use **integer arithmetic
only** for the hot path (sensor driver → zbus publish):

```c
/* Integer-only encode — safe in any context */
static inline int32_t temperature_c_x1000_to_q31(int32_t temp_milli_c)
{
    /* range: -40000 .. +85000 milli-°C */
    int64_t shifted = (int64_t)(temp_milli_c + 40000);   /* 0 .. 125000 */
    return (int32_t)((shifted * INT32_MAX) / 125000);
}

/* Float decode — acceptable in display/MQTT code only */
static inline double q31_to_temperature_c(int32_t q31) {
    return ((double)q31 / (double)INT32_MAX) * 125.0 - 40.0;
}
```

### Granularity example

One BME280 sample cycle produces **two events** on `sensor_event_chan`:

```
t=0ms   sensor_event_chan ← { uid=0x0001, TEMPERATURE, q31=..., ts=1000 }
t=0ms   sensor_event_chan ← { uid=0x0002, HUMIDITY,    q31=..., ts=1000 }
```

The MQTT publisher formats them as separate JSON messages:
```json
{"uid": 1, "type": "temperature", "value": 21.5, "ts": 1000}
{"uid": 2, "type": "humidity",    "value": 50.2, "ts": 1000}
```

The display routes them independently:
- `uid=0x0001` → look up registry → location="living_room" → update indoor temp tile
- `uid=0x0002` → look up registry → location="living_room" → update indoor hum tile

### Extensibility: adding a new sensor type

1. Add one entry to `enum sensor_type` in `sensor_event.h`
2. Add Q31 range mapping comment
3. Add encode/decode helpers to `q31.h`
4. Write a driver that publishes events with the new type

**Zero changes** to `struct env_sensor_data`, `sensor_event_chan`, display
manager, MQTT publisher, or any existing driver. Consumers that don't handle
the new type simply ignore the event (pattern: `switch(evt.type) { ... default: break; }`).

---

## Consequences

**Easier:**
- New sensor types are additive — no breaking changes to producers or consumers.
- The struct is trivially serialisable for LoRa (16 bytes), flash storage, and
  MQTT JSON.
- Filtering by type or uid is a simple equality check — no field inspection.
- The fixed size makes queue depth calculations exact.

**Harder:**
- Consumers that need to correlate temperature + humidity from the same chip
  (e.g. to compute dew point) must buffer both events and match by `sensor_uid`
  prefix or timestamp. This is slightly more code than reading a single struct
  with both fields.
- The Q31 range-per-type contract must be documented and respected. A driver
  that encodes pressure using the temperature range will produce silently wrong
  values.

**Constrained:**
- Only scalar (single-number) measurements fit this model. Vector or
  composite measurements (e.g. accelerometer XYZ) need a different approach
  — define a new channel with a suitable struct rather than overloading this one.
- `sensor_uid` values must be allocated carefully. A uid collision between two
  physical sensors will corrupt display routing. Maintain a uid registry table
  in the project documentation.

---

## Serialisation (cross-device boundary)

`env_sensor_data` is **intentionally not a wire format**.  It is an in-memory
zbus message: the struct layout may differ between 32-bit and 64-bit builds
(natural sizeof is 20 bytes on 32-bit, 24 bytes on 64-bit due to alignment
padding before `int64_t`), and it must never be cast to a byte array and
transmitted as-is to another device.

When a sensor event must cross a device boundary — LoRa radio, MQTT broker,
BLE, USB — a dedicated encoding layer will translate `env_sensor_data` into
a portable wire representation.  The format has not been chosen yet; options
include protobuf / nanopb, CBOR / zcbor, or a hand-rolled compact layout.  See
the backlog item **[SERIALIZATION]** for evaluation criteria and acceptance
conditions.

**Rule:** No code outside `lib/sensor_codec` (to be created) may serialise or
deserialise `env_sensor_data` directly.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Nested struct with all fields | Struct grows with every new type; consumers must know all fields; multiple measurements bundled per event |
| `union` over all value types | Union size = largest member; type safety lost; still grows with new types |
| Zephyr `sensor_value` struct (val1 + val2) | Tied to Zephyr driver API; not suitable for LoRa packet encoding or cross-node transfer |
| Floating-point value field | Unsafe in ISR/callback context; varies in encoding across architectures; larger struct |
| String-based representation | Expensive to encode/decode; no type safety; large messages |
