---
name: new-sensor-type
description: Use when adding a new fake sensor type (a new physical quantity with Q31 encoding, DT binding, driver and overlay node). Triggers: "add sensor type", "new sensor", "fake sensor", or an unsupported quantity.
argument-hint: <type_name> "<description>" <value_milli> <uid> <range_min> <range_max> <unit>
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Add a New Fake Sensor Type

Adds a physical quantity to the `fake_sensors` library. `scripts/scaffold-sensor.sh` builds the
driver from `lib/fake_sensors/src/fake_temperature.c` with `sed` rules, so do not hand-copy it.
Fake sensors are for native_sim and tests only. They must never be enabled in a hardware build.

**Arguments**: type_name=`$0` · description=`$1` · value_milli=`$2` · uid=`$3` · range_min=`$4` · range_max=`$5` · unit=`$6`

## Steps

1. **Branch**: `git checkout master && git pull && git checkout -b feat/fake-<type_name>`
2. **Pick the UID**. Read `apps/gateway/boards/native_sim.overlay` first. UIDs must be unique
   across all overlays. Ranges:
   - `0x0001`–`0x000F` indoor / gateway-local
   - `0x0011`–`0x001F` outdoor
   - `0x0021`+ remote sensor nodes
   - `0x0101`+ test-only
   The full allocation table is in `docs/architecture/architecture-constraints.md`.
3. **Dry run, then scaffold**:
   ```bash
   scripts/scaffold-sensor.sh <type_name> "<description>" <value_milli> <uid> <range_min> <range_max> <unit> --dry-run
   scripts/scaffold-sensor.sh <type_name> "<description>" <value_milli> <uid> <range_min> <range_max> <unit>
   ```
   Options: `--dry-run`, `--skip-build`, `--quiet`. JSON goes to stdout. Exit 0 on success,
   1 on error. The script:
   - adds `<type>_to_q31()` and `q31_to_<type>()` helpers to `sensor_event.h`
   - adds a `FAKE_SENSOR_KIND_<TYPE>` constant to `fake_sensors.h`
   - creates `dts/bindings/fake,<type_name>.yaml` and `lib/fake_sensors/src/fake_<type_name>.c`
   - registers the driver in `lib/fake_sensors/CMakeLists.txt`
   - appends the DT node to `apps/gateway/boards/native_sim.overlay`
4. **Check the init priority**. The script renames the init function and picks the first free
   `SYS_INIT` priority in 90–98 among fake sensors. Confirm with `grep -rn "SYS_INIT(fake_" lib/`.
5. **Verify the Q31 round trip**. Confirm that `q31_to_<type>(<type>_to_q31(x))` returns `x`
   within tolerance, and that the range span matches `range_max - range_min`.
6. **Consumers**: look up sensors through `sensor_registry` by `sensor_uid`. Never hardcode a UID.

## Verification

The DTS changed, so build pristine (`-p always`) from the workspace via `scripts/ws.sh`:

```bash
scripts/ws.sh 'west build -p always -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'
scripts/ws.sh 'west build -p always -b native_sim/native/64 -d build/native_sim_native_64/sensor-node weather-station/apps/sensor-node'
scripts/ws.sh 'printf "help\nfake_sensors list\n" | timeout 10 build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout'
scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/fake_sensors --inline-logs -v -N'
scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/sensor_event --inline-logs -v -N'
```

The `sensor_event` suite covers the Q31 helpers. Then run `/build-and-test` for the full gate and
`pre-commit run --all-files` before committing. Commit as `feat(fake-<type_name>): <summary>`.
