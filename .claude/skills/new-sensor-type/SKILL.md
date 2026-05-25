---
name: new-sensor-type
description: Use when adding a new fake sensor type to the weather-station project — a new physical quantity with a Q31 encoding. Invoke when the user says "add sensor type", "new sensor", "fake sensor", or mentions a quantity not yet supported.
argument-hint: <type_name> "<description>" <initial_value_milli> <sensor_uid>
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Add a New Fake Sensor Type

Use this when adding a **new physical quantity** that has an enum value in
`sensor_event.h` but no fake driver yet (e.g. CO₂, pressure, UV index, battery
voltage).

**Arguments received**: type_name=`$0` · description=`$1` · initial_value_milli=`$2` · sensor_uid=`$3`

---

## Step 0 — Branch

```bash
git checkout master && git pull
git checkout -b feat/fake-<type_name>
```

---

## Step 1 — Derive the Q31 range formula

Before writing any code, explicitly derive the encode/decode formulas.

**Template** (fill in `range_min` and `range_span` for the new quantity):
```
range_min  = <lowest physical value>
range_span = <highest physical value> - range_min

encode: q31 = (value - range_min) / range_span * INT32_MAX
decode: value = (double)q31 / INT32_MAX * range_span + range_min
```

**Example for CO₂ (400–5000 ppm, span = 4600):**
```
encode: q31 = (ppm - 400.0) / 4600.0 * INT32_MAX
decode: ppm = (double)q31 / INT32_MAX * 4600.0 + 400.0
```

Write these formulas down before proceeding — they drive the helper functions
and the `initial-value-milli` DT property.

**Validate:** Compute a round-trip: encode a known value to Q31, then decode it back. Confirm the result matches within floating-point precision. Add a comment in the source file showing the round-trip.

---

## Step 2 — Add the Q31 helpers to `sensor_event.h`

File: `lib/sensor_event/include/sensor_event/sensor_event.h`

Add two `static inline` functions after the existing helpers (lines ~76–109):

```c
/** @brief Encode <description> to Q31. */
static inline int32_t <type_name>_to_q31(double value)
{
    return (int32_t)((value - <range_min>) / <range_span> * (double)INT32_MAX);
}

/** @brief Decode Q31 to <description>. */
static inline double q31_to_<type_name>(int32_t q31)
{
    return (double)q31 / (double)INT32_MAX * <range_span> + <range_min>;
}
```

Verify `SENSOR_TYPE_<TYPE_NAME>` already exists in `enum sensor_type` (lines ~26–34).
If it is missing, add it — but **do not renumber** existing values.

---

## Step 3 — Add the `fake_sensor_kind` constant

File: `lib/fake_sensors/include/fake_sensors/fake_sensors.h`

Add `FAKE_SENSOR_KIND_<TYPE_NAME>` to the `fake_sensor_kind` enum, following the
existing pattern for `FAKE_SENSOR_KIND_TEMPERATURE` and `FAKE_SENSOR_KIND_HUMIDITY`.

---

## Step 4 — Add the DT binding

File: `dts/bindings/fake,<type_name>.yaml`  (create new file)

Model after `dts/bindings/fake,temperature.yaml`:
```yaml
description: Fake <type_name> sensor for native_sim testing

compatible: "fake,<type_name>"

properties:
  sensor-uid:
    type: int
    required: true
    description: Unique sensor identifier (matches sensor_registry UID)

  location:
    type: string
    required: true
    description: Human-readable location label

  initial-value-m<unit>:
    type: int
    required: true
    description: Initial <description> in milli-<unit>

status:
  required: false
  default: okay
```

---

## Step 5 — Write the fake driver

File: `lib/fake_sensors/src/fake_<type_name>.c`  (create new file)

**Copy `lib/fake_sensors/src/fake_temperature.c` verbatim, then replace tokens per the table in [`references/token-replace-table`](references/token-replace-table).**

The token-replace table lists every identifier that must change when creating a new sensor type from the temperature template. Follow it exactly — don't invent substitutions.

---

## Step 6 — Register the new source in CMakeLists.txt

File: `lib/fake_sensors/CMakeLists.txt`

Add `src/fake_<type_name>.c` to the `zephyr_library_sources(...)` call:

```cmake
zephyr_library_sources(src/fake_temperature.c src/fake_humidity.c src/fake_<type_name>.c)
```

---

## Step 7 — Add DT nodes to overlays

File: `apps/gateway/boards/native_sim.overlay`

UID allocation rules:
- `0x0001–0x000F` → indoor / gateway-local sensors
- `0x0011–0x001F` → outdoor sensors
- `0x0021+` → remote sensor nodes
- `0x0101+` → test-only

Always read the overlay first to find the next free UID in the correct range.

```dts
fake_<type_name>_indoor: fake-<type_name>-indoor {
    compatible = "fake,<type_name>";
    sensor-uid = <sensor_uid>;
    location = "living_room";
    initial-value-m<unit> = <initial_value_milli>;
    status = "okay";
};
```

If applicable, also add to `apps/sensor-node/boards/native_sim.overlay` using UIDs from `0x0021+`.

---

## Step 8 — Verify the build

Kconfig and DTS changed → pristine rebuild required:

```bash
west build -p always -b native_sim/native/64 apps/gateway
west build -p always -b native_sim/native/64 apps/sensor-node
```

**Validate:** Confirm "BUILD SUCCESS" and no unresolved symbol errors for the new sensor type.

Shell smoke-test:

```bash
printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

**Validate:** The new sensor must appear in `fake_sensors list` output. Confirm the sensor UID and initial value match what was configured.

Then proceed to `/build-and-test` for the full gate.

---

## Step 9 — Commit

```bash
git add lib/sensor_event/include/sensor_event/sensor_event.h \
        lib/fake_sensors/include/fake_sensors/fake_sensors.h \
        lib/fake_sensors/src/fake_<type_name>.c \
        lib/fake_sensors/CMakeLists.txt \
        dts/bindings/fake,<type_name>.yaml \
        apps/gateway/boards/native_sim.overlay \
        apps/sensor-node/boards/native_sim.overlay

git commit -m "feat(fake_sensors): add fake_<type_name> driver

Add fake_<type_name>.c driver for SENSOR_TYPE_<TYPE_NAME>.
Q31 range: <range_min>–<range_max> <unit>, span=<range_span>.
UID <sensor_uid> assigned to living_room instance in gateway overlay.
SYS_INIT priority <priority> (APPLICATION)."
```

---

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "I need a sensor_manager to poll this sensor" | ADR-004: trigger-driven, not polled. Subscribe to `sensor_trigger_chan`. |
| "I'll use `target_link_libraries()` in the app" | ADR-008: Kconfig-only composition. Never. |
| "I can define `sensor_event_chan` in my driver" | Already defined in `lib/sensor_event/src/sensor_event.c`. Don't redefine. |
| "I'll pick UID 0x0001" | Read the overlay first. UIDs must be unique across all overlays. |
| "I'll hardcode the UID in the consumer" | Use `sensor_registry`. UIDs are DT-derived, not hardcoded. |

## Next steps

After completing this skill:
- Invoke `/build-and-test` to run the full verification gate
