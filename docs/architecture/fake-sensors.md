# Fake Sensor Subsystem

> Design rationale: [ADR-005](../adr/ADR-005-fake-sensor-subsystem.md).

The fake sensor subsystem is a first-class library module in `lib/fake_sensors/`. It is not a test stub — it is the primary sensor backend for `native_sim` development and CI. Fake sensors are indistinguishable from real sensor drivers from the perspective of all consumers (display, MQTT, HTTP dashboard).

---

## Module structure

```
lib/fake_sensors/
├── CMakeLists.txt          ← conditional on CONFIG_FAKE_SENSORS
├── Kconfig                 ← menuconfig FAKE_SENSORS + AUTO_PUBLISH_MS
├── include/fake_sensors/
│   └── fake_sensors.h      ← FAKE_SENSOR_REGISTER macro + entry struct
└── src/
    ├── fake_temperature.c  ← DT_FOREACH driver for fake,temperature nodes
    ├── fake_humidity.c     ← DT_FOREACH driver for fake,humidity nodes
    ├── fake_subsystem.c    ← STRUCT_SECTION_ITERABLE registry + find_by_uid
    └── fake_shell.c        ← shell commands
```

---

## Devicetree binding

Each sensor type has a custom `compatible` string defined in `dts/bindings/`. The binding enforces required properties at build time. Example for `fake,temperature`:

```yaml
description: Fake temperature sensor for native_sim and testing

compatible: "fake,temperature"

properties:
  sensor-uid:
    type: int
    required: true
  location:
    type: string
    required: true
  initial-value-mdegc:
    type: int
    default: 20000
    description: Initial value in milli-degrees Celsius (20000 = 20.0°C)
```

See `dts/bindings/` for the canonical binding YAML files for all supported types.

---

## Devicetree usage

Fake sensors are declared in board overlays using the custom `compatible` strings. The build system discovers them automatically when `CONFIG_FAKE_SENSORS=y`:

```dts
/ {
    fake_sensors {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <0>;

        fake_temp_indoor: fake_temperature@0 {
            compatible = "fake,temperature";
            reg = <0>;
            sensor-uid = <0x0001>;
            location = "living_room";
            initial-value-mdegc = <21000>;
        };

        fake_hum_indoor: fake_humidity@0 {
            compatible = "fake,humidity";
            reg = <0>;
            sensor-uid = <0x0002>;
            location = "living_room";
            initial-value-mpct = <50000>;
        };
    };
};
```

---

## STRUCT_SECTION_ITERABLE registration

Each driver instance registers itself into the fake sensor subsystem. The canonical definition of `fake_sensor_entry` and `FAKE_SENSOR_REGISTER` is in `lib/fake_sensors/include/fake_sensors/fake_sensors.h`:

```c
struct fake_sensor_entry {
    uint32_t         uid;
    enum sensor_type sensor_type;
    const char      *location;
    void            *data;               /* driver instance data */
    int            (*publish_fn)(void *data);
    int            (*set_fn)(void *data, int32_t raw_value);
};

/* Each driver calls this once per DT instance via DT_FOREACH */
#define FAKE_SENSOR_REGISTER(_node_id, _data_ptr, _pub_fn, _set_fn)   \
    STRUCT_SECTION_ITERABLE(fake_sensor_entry,                         \
        _fake_entry_##_node_id) = {                                    \
        .uid         = DT_PROP(_node_id, sensor_uid),                  \
        .sensor_type = /* derived from compatible */,                  \
        .location    = DT_PROP(_node_id, location),                    \
        .data        = _data_ptr,                                      \
        .publish_fn  = _pub_fn,                                        \
        .set_fn      = _set_fn,                                        \
    }
```

The linker collects all `STRUCT_SECTION_ITERABLE(fake_sensor_entry, ...)` instances into a contiguous array. `fake_sensors_find(uid)` iterates this array — no dynamic allocation, no hash map.

---

## Shell interaction

Values are set via `fake_sensors <type>_set <uid> <value>` shell commands. Setting a value immediately publishes a `sensor_event_chan` event — the display updates in real time and MQTT publishes the new reading:

```
uart:~$ fake_sensors list
 UID     TYPE         LOCATION       CURRENT
 0x0001  temperature  living_room    21.000 °C
 0x0002  humidity     living_room    50.000 %RH
 0x0011  temperature  outdoor         4.000 °C
 0x0012  humidity     outdoor        30.000 %RH

uart:~$ fake_sensors temperature_set 17 -3500
[00:00:42.001] <inf> fake_temp: uid=0x0011 → -3.500°C published

uart:~$ fake_sensors humidity_set 18 92000
[00:00:45.003] <inf> fake_hum:  uid=0x0012 → 92.000%RH published
```

---

## Auto-publish

When non-zero, `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS` starts a timer that re-publishes all current fake values at the given interval. This simulates a continuously streaming sensor without shell interaction — useful for integration tests:

```ini
CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS=5000
```

The timer is implemented in `lib/fake_sensors/src/fake_sensors_timer.c` and starts automatically via `SYS_INIT`. No call from `main.c` is needed.

---

## Data flow

```mermaid
--8<-- "fake-sensor-flow.mmd"
```
