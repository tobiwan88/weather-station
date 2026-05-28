# REQ-DATA — Data Bus and Event Requirements

## Status
review

## Version
0.1

## Context
The data bus (zbus) is the system-wide publish/subscribe fabric connecting all subsystems. This document defines the channel ownership rules, event struct contracts, and data flow patterns that all libraries must follow.

## Functional Requirements

### REQ-DATA-001 — zbus Channel Ownership
The system shall define each zbus channel (`ZBUS_CHAN_DEFINE`) in exactly one `.c` file. Public headers shall contain only `ZBUS_CHAN_DECLARE` for consumer use.

### REQ-DATA-002 — Sensor Event Struct
The system shall use `env_sensor_data` as the sensor event struct — a flat 20-byte structure with Q31 fixed-point values, sensor_uid, timestamp, and measurement type. No heap pointers.

### REQ-DATA-003 — Ring Buffer Snapshot Pattern
The system shall protect shared data structures (sensor data ring buffer) with `k_spinlock`, copying snapshots within the lock and processing outside the lock. JSON serialization shall occur outside the lock.

### REQ-DATA-004 — Listener Non-Blocking Contract
The system shall ensure zbus listeners do not block or sleep, as they run in the publisher's thread context. Heavy work shall be deferred to subscriber threads.

### REQ-DATA-005 — Sequential Listener Dispatch
The system shall dispatch zbus listeners sequentially in registration order. A blocking listener stalls all subsequent listeners.

### REQ-DATA-006 — Copyable Message Structs
The system shall ensure all zbus message structs are copyable (no pointers to heap memory).

### REQ-DATA-007 — Subscriber Queue Depth
The system shall tune subscriber queue depth to prevent missed messages under load. Queue depth shall be configurable via Kconfig.

### REQ-DATA-008 — Library Isolation
The system shall enforce zero `#include` dependencies between library internal headers. Libraries communicate only via zbus channels, with two exceptions:
- `sensor_registry` and `location_registry` expose public read-only APIs
- Integration files compiled conditionally when two CONFIG symbols are both set

### REQ-DATA-009 — Display/Connectivity Separation
The system shall maintain zero `#include` dependencies between `lib/lvgl_display/` and `lib/connectivity/` libraries, enforced by CI include-path checks.

### REQ-DATA-010 — Producer-Consumer Decoupling
The system shall ensure producers never know about consumers. Adding a new consumer shall not require changes to producer code.

### REQ-DATA-011 — Channel Map
The system shall maintain the following zbus channels:
| Channel | Owner | Direction |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | trigger sources → sensor drivers |
| `sensor_event_chan` | `lib/sensor_event` | sensor drivers → consumers |
| `config_cmd_chan` | `lib/config_cmd` | config producers → config consumers |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | manager/shell → transport adapters |
| `remote_discovery_chan` | `lib/remote_sensor` | transport adapters → manager |
| `remote_peer_cmd_chan` | `remote_sensor` | Manager → Transports |
| `lora_link_chan` | `lora_radio` | LoRa → Diagnostics |
| `lora_fota_chan` | `lora_radio` | HTTP → LoRa |

### REQ-DATA-012 — Flat Struct Size Limit
The system shall enforce `sizeof(env_sensor_data) <= 32` via `BUILD_ASSERT` at compile time. Actual size: 20 bytes on 32-bit target.

### REQ-DATA-013 — Protobuf Wire Format (Deferred)
The system shall define a protobuf wire format for cross-device communication (sensor node → gateway, gateway → cloud). This is a deferred requirement tracked in backlog as `[SERIALIZATION]`.

## Dependencies
- REQ-SENSORS-001: Flat event data model
- REQ-SENSORS-002: Q31 encoding contract
- All subsystem requirements depend on zbus channel contracts

## Constraints
- `env_sensor_data` size: 20 bytes, BUILD_ASSERT <= 32
- zbus listeners must not block or acquire mutexes
- No heap allocation in message structs
- Subscriber queue depth must prevent message loss

## Acceptance Criteria
- [ ] [native_sim] ztest: ZBUS_CHAN_DEFINE in one .c, ZBUS_CHAN_DECLARE in header
- [ ] [native_sim] ztest: env_sensor_data struct size <= 32 bytes
- [ ] [native_sim] ztest: Message struct is copyable (no heap pointers)
- [ ] [native_sim] Integration: Adding new subscriber does not require producer changes
- [ ] [native_sim] Integration: Ring buffer snapshot completes within spinlock
- [ ] [native_sim] Integration: Blocking listener stalls subsequent listeners
- [ ] [native_sim] ztest: No #include of connectivity headers in display source
- [ ] [native_sim] ztest: No #include of display headers in connectivity source
- [ ] [renode] Integration: zbus events flow correctly across simulated subsystems

## Related ADRs
- ADR-002: zbus as System-Wide Communication Fabric
- ADR-003: Sensor Event Data Model (flat struct + Q31)
- ADR-007: Gateway and Display as One Device (library isolation)
- ADR-008: Kconfig-Only App Composition

## Related Zephyr subsystems
- CONFIG_ZBUS
- CONFIG_ZBUS_CHAN_STATIC
- CONFIG_ZBUS_SUBSCRIBER_QUEUE
