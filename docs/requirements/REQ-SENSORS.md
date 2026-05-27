# REQ-SENSORS — Sensor Subsystem Requirements

## Status
review

## Version
0.1

## Context
Sensor drivers collect environmental measurements (temperature, humidity, pressure, CO2, VOC, PM2.5/PM10, gas resistance) and publish them as flat events via zbus. The system supports both fake sensors (native_sim development) and real hardware (BME680, SEN0460, etc.). All sampling is trigger-driven with no central sensor manager.

## Functional Requirements

### REQ-SENSORS-001 — Flat Event Data Model
The system shall publish each sensor measurement as a flat `env_sensor_data` struct (20 bytes) containing Q31 fixed-point values, sensor_uid, timestamp, and measurement type.
- **Constraint:** `sizeof(env_sensor_data) <= 32` enforced by `BUILD_ASSERT` at compile time.
- **Constraint:** No heap pointers — struct is copied by value through zbus.

### REQ-SENSORS-002 — Q31 Encoding Contract
The system shall encode all physical measurements as Q31 fixed-point values in the range [-1.0, +1.0], scaled per measurement type so that the full physical range fits within the Q31 range.
- Temperature: scaled so -40 °C to +85 °C fits.
- Humidity: scaled so 0% to 100% fits.

### REQ-SENSORS-003 — Trigger-Driven Sampling
The system shall sample sensors via `sensor_trigger_chan` zbus channel — no polling loops, no per-sensor timers, no sensor manager module.
- Trigger callbacks must not block or sleep — defer blocking I2C/SPI reads to `k_work` items.
- Broadcast trigger wakes all sensors simultaneously.

### REQ-SENSORS-004 — One Event = One Measurement
The system shall publish exactly one zbus event per individual measurement. Correlated readings (e.g., temperature + humidity from BME680) are published as separate events with the same sensor_uid.

### REQ-SENSORS-005 — Sensor UID Identity
The system shall use `sensor_uid` (16-bit) as the unique identity key for every sensor. UIDs shall never be hardcoded in consumer code — use `sensor_registry` for lookup.
- Fake sensor UIDs: 0x0001–0x00FF
- Real hardware UIDs: 0x0100+
- Gateway-local/indoor: 0x0001–0x000F
- Gateway outdoor: 0x0011–0x001F
- Remote nodes: 0x0021–0x00FF

### REQ-SENSORS-006 — Fake Sensor Subsystem
The system shall provide fake sensors as a production-quality library (`lib/fake_sensors/`) that is devicetree-defined, shell-interactive, trigger-driven, and self-registering via `STRUCT_SECTION_ITERABLE`.
- `CONFIG_FAKE_SENSORS` shall never be `y` in production/release builds.
- Fake sensors shall auto-publish at configurable intervals via `CONFIG_FAKE_SENSOauto_publish_ms`.

### REQ-SENSORS-007 — BME680 Forced-Mode Timing
The system shall operate the BME680 in forced mode, triggering a new measurement cycle on each trigger event and reading results after the sensor's internal conversion completes.

### REQ-SENSORS-008 — SEN0460 Settle Time
The system shall allow the SEN0460 (CO2) sensor adequate settle time after power-on before publishing valid readings.

### REQ-SENSORS-009 — Devicetree Instantiation
The system shall instantiate all sensor drivers via devicetree nodes (`DEVICE_DT_DEFINE`), never through runtime registration calls.

### REQ-SENSORS-010 — No Heap Allocation
The system shall not use `malloc`, `free`, `k_malloc`, or `k_free` in any sensor driver. All allocation shall be static (stack, BSS, `static`).

## Dependencies
- REQ-DATA-001: zbus channel ownership for sensor events
- REQ-DATA-002: sensor event struct definition

## Constraints
- Memory: `env_sensor_data` = 20 bytes per event
- No heap allocation anywhere in sensor path
- zbus listeners must not block (run in publisher's context)

## Acceptance Criteria
- [ ] [native_sim] ztest: `env_sensor_data` struct size <= 32 bytes via BUILD_ASSERT
- [ ] [native_sim] ztest: Q31 encode/decode round-trip for temperature, humidity, pressure
- [ ] [native_sim] Integration: fake sensors publish events on trigger via sensor_trigger_chan
- [ ] [native_sim] Integration: sensor_registry returns correct label/location for registered UIDs
- [ ] [native_sim] Shell: `fake_sensors list` shows all registered fake sensors
- [ ] [renode] Integration: BME680 forced-mode cycle completes within timing budget
- [ ] [hil] Board: BME680 produces valid readings on FRDM-MCXN947
- [ ] [hil] Board: SEN0460 settle time respected before first valid CO2 reading

## Related ADRs
- ADR-003: Sensor Event Data Model (flat struct + Q31)
- ADR-004: Trigger-Driven Sensor Sampling (no sensor manager)
- ADR-005: Fake Sensor Subsystem for native_sim

## Related Zephyr subsystems
- CONFIG_SENSOR
- CONFIG_SENSOR_ASYNC
- CONFIG_I2C
- CONFIG_SPI
- CONFIG_GPIO
- CONFIG_BME680
- CONFIG_SCD4X
