# REQ-POWER — Power Management Requirements

## Status
review

## Version
0.1

## Context
Power management covers sleep states, wake sources, sensor power integration, and outdoor sensor node duty cycle. Power consumption cannot be measured or optimized on native_sim — verification requires hardware.

## Functional Requirements

### REQ-POWER-001 — Sleep State Support
The system shall support entering low-power sleep states when no active work is pending, with configurable wake sources.

### REQ-POWER-002 — Wake Sources
The system shall wake from sleep on:
- Sensor trigger events
- LoRa packet reception
- Timer expiry (SNTP resync, keep-alive)
- Button press (gateway)
- UART activity (debug/bridge)

### REQ-POWER-003 — BME680 Power Integration
The system shall power down the BME680 between measurement cycles in forced mode, minimizing standby current draw.

### REQ-POWER-004 — SEN0460 Power Integration
The system shall manage SEN0460 power state, allowing adequate warm-up time after power-on before taking valid CO2 readings.

### REQ-POWER-005 — Outdoor Node Duty Cycle
The system shall enforce duty cycle limits on outdoor sensor nodes to minimize power consumption:
- Minimum transmit interval: ~37 seconds at SF10/BW125 (1% EU868 duty cycle)
- Change-threshold gating to avoid unnecessary transmissions
- Keep-alive timer to detect and recover from silent-node syndrome

### REQ-POWER-006 — LoRa Duty Cycle Enforcement
The system shall track LoRa transmission time on a sliding window and enforce 1% duty cycle:
- At 0.9%: defer change-triggered TX
- At 1.0%: skip all TX and buffer readings

### REQ-POWER-007 — Power Measurement Exclusion
The system shall not attempt to measure or optimize power consumption on native_sim. Power tests shall run on hardware only.

## Dependencies
- REQ-LORA-011: LoRa duty cycle enforcement
- REQ-LORA-012: Change-threshold gating
- REQ-LORA-013: Keep-alive timer
- REQ-SENSORS-007: BME680 forced-mode timing
- REQ-SENSORS-008: SEN0460 settle time

## Constraints
- Power consumption cannot be measured on native_sim (ADR-009)
- Minimum transmit interval at SF10/BW125: ~37 seconds
- LoRa duty cycle: 1% EU868 sliding window

## Acceptance Criteria
- [ ] [native_sim] ztest: Sleep state entry/exit does not crash
- [ ] [native_sim] Integration: Wake sources trigger correct handlers
- [ ] [native_sim] Integration: Duty cycle enforcement defers TX at threshold
- [ ] [hil] Power test: BME680 standby current within datasheet spec
- [ ] [hil] Power test: Outdoor node average current within budget
- [ ] [hil] Power test: Sleep state current within target µA range
- [ ] [manual] Board: Power profiling with multoscope/Power Profiler Kit

## Related ADRs
- ADR-009: native_sim First — No Hardware Dependency in v1
- ADR-015: LoRa Protocol Architecture (duty cycle, keep-alive)

## Related Zephyr subsystems
- CONFIG_PM
- CONFIG_PM_DEVICE
- CONFIG_PM_DEVICE_RUNTIME
- CONFIG_SYS_POWER_MANAGEMENT
- CONFIG_SYSTEM_SLEEP
