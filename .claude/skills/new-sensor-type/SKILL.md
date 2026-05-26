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

## Use the scaffold script

Instead of creating files manually, use the scaffold script:

```bash
scripts/scaffold-sensor.sh <type_name> "<description>" <value_milli> <uid> <range_min> <range_max> <unit>
```

**Options:**
- `--dry-run` — Show what would be created without writing files
- `--skip-build` — Skip the build verification step
- `--quiet` — Suppress stderr progress output

**Example:**
```bash
scripts/scaffold-sensor.sh co2 "CO2 concentration" 412000 0x0003 400.0 5000.0 ppm
scripts/scaffold-sensor.sh co2 "CO2 concentration" 412000 0x0003 400.0 5000.0 ppm --dry-run
```

**Output:** JSON to stdout with created/modified files list, Q31 range info, build status, and overall verdict. Human-readable progress goes to stderr.

**JSON output schema:**
```json
{
  "script": "scaffold-sensor",
  "timestamp": "2026-05-25T12:00:00Z",
  "verdict": "PASS|FAIL|DRY_RUN",
  "type_name": "co2",
  "sensor_uid": "0x0003",
  "q31_range": {
    "min": 400.0,
    "max": 5000.0,
    "span": 4600.0,
    "unit": "ppm"
  },
  "files_created": [
    "dts/bindings/fake,co2.yaml",
    "lib/fake_sensors/src/fake_co2.c"
  ],
  "files_modified": [
    "lib/sensor_event/include/sensor_event/sensor_event.h",
    "lib/fake_sensors/include/fake_sensors/fake_sensors.h",
    "lib/fake_sensors/CMakeLists.txt",
    "apps/gateway/boards/native_sim.overlay"
  ],
  "build_status": "PASS|FAIL|SKIPPED|N/A",
  "build_output": "...",
  "summary": "Created 2 files, modified 4 files for fake_co2."
}
```

**Exit codes:** 0 = success, 1 = error (missing args, file exists, build failed)

## Manual steps (for reference)

The script performs these steps:

1. **Branch** — `git checkout master && git pull && git checkout -b feat/fake-<type_name>`
2. **Derive the Q31 range formula** — Compute `range_min`, `range_span`, encode/decode formulas. Validate with a round-trip test.
3. **Add Q31 helpers** to `sensor_event.h` — `<type_name>_to_q31()` and `q31_to_<type_name>()`
4. **Add `fake_sensor_kind` constant** to `fake_sensors.h`
5. **Create DT binding** — `dts/bindings/fake,<type_name>.yaml`
6. **Create fake driver** — Copy `fake_temperature.c` and replace tokens per [`references/token-replace-table`](references/token-replace-table)
7. **Register in CMakeLists.txt** — Add `src/fake_<type_name>.c`
8. **Add DT node to overlays** — Use UID allocation rules (0x0001–0x000F indoor, 0x0011–0x001F outdoor, 0x0021+ remote)
9. **Verify the build** — Pristine rebuild required (DTS changed)

## UID allocation rules

- `0x0001–0x000F` → indoor / gateway-local sensors
- `0x0011–0x001F` → outdoor sensors
- `0x0021+` → remote sensor nodes
- `0x0101+` → test-only

Always read the overlay first to find the next free UID in the correct range.

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
