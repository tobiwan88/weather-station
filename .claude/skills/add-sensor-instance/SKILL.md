---
name: add-sensor-instance
description: Add a new DT node for an existing fake sensor driver at a new location. Does not create a new driver. Accepts five arguments.
argument-hint: <type_name> <location> <sensor_uid> <initial_value_milli> <app>
disable-model-invocation: true
---

# Add a Sensor Instance (Existing Driver)

Use this when adding **another sensor at a new location** using a driver that
already exists — e.g. a bedroom temperature sensor, an attic humidity sensor.
The fake driver (`.c` file and DT binding) is already present; you only need to
add a DT node and verify consumer capacity.

**Arguments received**: type_name=`$0` · location=`$1` · sensor_uid=`$2` · initial_value_milli=`$3` · app=`$4`

---

## Step 0 — Branch

```bash
git checkout master && git pull
git checkout -b feat/sensor-<type_name>-<location>
```

---

## Step 1 — Check the UID namespace

Read the target overlay to find all UIDs already in use:

File: `apps/<app>/boards/native_sim.overlay`

UID allocation rules:

| Range | Purpose |
|---|---|
| `0x0001–0x000F` | Gateway-local / indoor sensors |
| `0x0011–0x001F` | Gateway outdoor sensors |
| `0x0021–0x00FF` | Remote sensor nodes |
| `0x0101+` | Test-only instances |

Choose the **lowest free UID** in the appropriate range for `<location>`.

**Never reuse a UID** that appears in any overlay file (gateway or sensor-node).
UIDs are the identity key used by `sensor_registry`, the LVGL display, and any
future MQTT publisher.

---

## Step 2 — Add the DT node

File: `apps/<app>/boards/native_sim.overlay`

Append inside the root `/ { ... };` block:

```dts
fake_<type_name>_<location>: fake-<type_name>-<location> {
    compatible = "fake,<type_name>";
    sensor-uid = <<sensor_uid>>;
    location = "<location>";
    initial-value-m<unit> = <<initial_value_milli>>;
    status = "okay";
};
```

Unit suffix by type:

| type_name | DT property suffix | unit |
|---|---|---|
| `temperature` | `initial-value-mdegc` | milli-°C |
| `humidity` | `initial-value-mpct` | milli-% |
| `co2` | `initial-value-mppm` | milli-ppm |
| `pressure` | `initial-value-mpa` | milli-hPa |
| `uv_index` | `initial-value-muvi` | milli-UV-index |
| `battery_mv` | `initial-value-mmv` | micro-V (already milli) |

---

## Step 3 — Check LVGL display consumer capacity (gateway only)

File: `lib/lvgl_display/src/lvgl_display.c`

If adding a sensor to the gateway app, check whether the LVGL display has a
fixed-size array of sensor slots. If so, verify the new sensor fits:

- Search for `MAX_SENSORS`, `sensor_slots`, or equivalent capacity constant.
- If the array is full, increase the capacity constant by 1 before proceeding.

This step is not needed for sensor-node instances.

---

## Step 4 — Run the build gate

DTS changed → pristine rebuild is required:

```bash
west build -p always -b native_sim/native/64 apps/<app>

# Shell smoke-test — verify the new sensor appears
printf "fake_sensors list\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Expected output: the new sensor appears with UID `<sensor_uid>` and location
`<location>`.

Run the full gate after smoke-test passes:

```bash
west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
pre-commit run --all-files
```

---

## Step 5 — Commit

```bash
git add apps/<app>/boards/native_sim.overlay
git commit -m "feat(fake_sensors): add <type_name> sensor at <location>

New DT node fake-<type_name>-<location>, UID <sensor_uid>.
Instantiates existing fake_<type_name> driver at location '<location>'.
Initial value: <initial_value_milli> milli-<unit>."
```

---

## Common mistakes to avoid

- **Do not** duplicate a UID that is already present in any overlay file
- **Do not** add a new `.c` file — the driver already exists; only the DT node is new
- **Do not** modify `lib/fake_sensors/CMakeLists.txt` — no new source file is needed
- **Do not** add Kconfig changes — the existing `CONFIG_FAKE_SENSORS=y` already enables
  all instances discovered via `DT_FOREACH_STATUS_OKAY`
- **Do not** hardcode UIDs in consumer code — always use `sensor_registry` lookups
- If adding to sensor-node, use UIDs from `0x0021+` (remote node range), not `0x0001`
