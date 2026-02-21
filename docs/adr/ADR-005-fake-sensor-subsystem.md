# ADR-005 — Fake Sensor Subsystem for native_sim

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

The project targets `native_sim` before any hardware is purchased or selected.
This means no real I2C sensors, no SPI LoRa modules, no physical buttons.
However, the full application logic — trigger-driven sampling, zbus routing,
display update, MQTT publish — must be exercisable without hardware.

Options for providing sensor data in simulation:

1. **Hardcoded constant values in `main.c`** — Simple but untestable and
   non-interactive. Can't simulate outdoor temperature going negative, or
   a spike in CO2 to verify display colour changes.

2. **Zephyr emulator framework** (`CONFIG_EMUL=y`) — Emulates I2C/SPI bus
   transactions at the driver level. Requires implementing the sensor register
   map. Correct but very low-level for this use case.

3. **`native_sim` fake drivers that implement the same trigger/publish pattern
   as real drivers, with shell-settable values** — Interactive, realistic,
   and directly tests the application logic path.

The choice also has a significant impact on **AI-assisted development**: when
asking Claude to write a new feature, the fake sensor subsystem must be
self-documenting and mechanical. A new contributor (or AI agent) should be
able to add a new fake sensor type by reading one existing fake driver and
copying the pattern — with no hidden wiring.

---

## Decision

Implement a **fake sensor subsystem** as a first-class, production-quality
library module in `lib/fake_sensors/`. It is not a test stub — it is the
primary sensor backend for `native_sim` development.

### Key properties

1. **Devicetree-defined.** Fake sensors are declared in board overlays using
   custom `compatible` strings. The Zephyr build system discovers them
   automatically when `CONFIG_FAKE_SENSORS=y`.

2. **Shell-interactive.** Values are set via `fake_sensors <type>_set <uid> <value>`
   shell commands. This allows live manipulation of sensor readings during
   a running simulation.

3. **Trigger-driven.** Fake sensors subscribe to `sensor_trigger_chan` and
   publish to `sensor_event_chan` — identical behaviour to real sensor drivers.
   From the perspective of consumers (display, MQTT), fake and real sensors
   are indistinguishable.

4. **Self-registering.** Each fake driver instance registers itself into the
   fake sensor subsystem via `STRUCT_SECTION_ITERABLE`. The shell commands
   enumerate all registered instances — no central list to maintain.

5. **Swap to real hardware by Kconfig only:**
   ```ini
   # native_sim:
   CONFIG_FAKE_SENSORS=y
   CONFIG_BME280=n

   # real hardware:
   CONFIG_FAKE_SENSORS=n
   CONFIG_BME280=y
   CONFIG_I2C=y
   ```
   No source code changes. The board overlay swaps the DT node compatible.

### Module structure

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

### Devicetree binding (`dts/bindings/fake,temperature.yaml`)

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

### Devicetree usage (`boards/native_sim.overlay`)

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

### Registration macro (`include/fake_sensors/fake_sensors.h`)

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

The linker collects all `STRUCT_SECTION_ITERABLE(fake_sensor_entry, ...)` 
instances into a contiguous array. `fake_sensors_find(uid)` iterates this
array — no dynamic allocation, no hash map.

### Shell interaction

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

Setting a value immediately publishes a `sensor_event_chan` event. The display
updates in real time. MQTT publishes the new reading. The full data flow is
exercised interactively.

### Optional auto-publish

```ini
CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS=5000
```

When non-zero, a timer re-publishes all current fake values at the given
interval. This simulates a continuously streaming sensor without shell
interaction — useful for integration tests.

### The full data flow (fake sensors)

```
Board overlay (native_sim.overlay)
         │
         ▼ (DT_FOREACH_STATUS_OKAY at compile time)
fake_temperature.c generates one instance struct per node
         │
         ▼ (SYS_INIT at APPLICATION priority)
Instance subscribes to sensor_trigger_chan
Instance registers into fake_sensor_entry iterable section
Instance registers metadata in sensor_registry
         │
         ▼ (at runtime, on trigger)
on_trigger() → temperature_c_x1000_to_q31() → zbus_chan_pub(sensor_event_chan)
         │
         ▼
[display_manager subscriber] → update Living Room tile
[mqtt_manager subscriber]    → publish JSON to Mosquitto
```

---

## Consequences

**Easier:**
- Full application logic is exercisable from day one — no hardware required.
- Shell-settable values enable manual and automated testing of edge cases
  (below-freezing outdoor temp, 100% humidity, battery low voltage).
- Adding a new fake sensor type requires one new file (`fake_co2.c`) and one
  new DT binding YAML — zero changes to existing code.
- CI tests can use `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS` to exercise the full
  pipeline automatically.

**Harder:**
- The fake sensor values are not realistic — they don't drift, they don't have
  noise. Realism requires either shell scripting or future enhancement with a
  configurable noise/drift model.
- Developers must remember to set `CONFIG_FAKE_SENSORS=n` when building for
  real hardware. Kconfig default is `n` specifically to prevent accidental
  inclusion.

**Constrained:**
- `CONFIG_FAKE_SENSORS` must never be `y` in a production/release build.
  Enforce this with a Kconfig `depends on !RELEASE` guard or CI check.
- Fake sensor UIDs must be reserved and not reused for real hardware sensors.
  Convention: fake sensors use UIDs 0x0001–0x00FF; real hardware starts at 0x0100.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Hardcoded constants in `main.c` | Not interactive; can't test edge cases; not representative of real data flow |
| Zephyr I2C emulator (`CONFIG_EMUL=y`) | Correct at the protocol level but requires implementing sensor register maps; much more code for the same testing benefit |
| Conditional `#ifdef` in real drivers | Pollutes production code with simulation logic; not easily extensible; violates separation of concerns |
| Python host-side value injection via UART | Extra tooling dependency; delays; doesn't test the actual sensor driver path |
| No simulation — buy hardware first | Blocks development; expensive iteration; CI impossible without hardware farm |
