# REQ-LOCATION — Location Registry Requirements

## Status
review

## Version
0.1

## Context
The location registry provides runtime CRUD for named physical locations, replacing compile-time devicetree location properties. Locations are used by the display for card labels, HTTP dashboard for JSON responses, and MQTT for topic hierarchy.

## Functional Requirements

### REQ-LOCATION-001 — Named Location CRUD
The system shall support runtime creation, reading, updating, and deletion of named physical locations. Each location shall have a unique identifier and a human-readable name.

### REQ-LOCATION-002 — Settings Persistence
The system shall persist location definitions across reboots using Zephyr settings subsystem under the `config/location/` namespace.

### REQ-LOCATION-003 — UID-Independent Metadata
The system shall store location metadata independently of sensor UIDs. Sensors reference locations by location ID, not by UID.

### REQ-LOCATION-004 — Display Integration
The system shall provide location names to the display subsystem for sensor card labels via `sensor_registry_lookup(uid)->location`.

### REQ-LOCATION-005 — HTTP API Integration
The system shall provide location data to the HTTP dashboard for JSON responses via `GET /api/locations`.

### REQ-LOCATION-006 — MQTT Topic Integration
The system shall include location names in MQTT topic hierarchy: `{gateway_id}/{location}/{sensor_name}/{measurement_type}`.

### REQ-LOCATION-007 — SYS_INIT Loading
The system shall load persisted location names at SYS_INIT priority 96, after the registry is initialized but before consumers need location data.

## Dependencies
- REQ-CONFIG-003: Settings persistence
- REQ-DISPLAY-005: Sensor registry routing for card labels
- REQ-HTTP-003: REST API for location listing
- REQ-MQTT-002: Topic hierarchy with location names

## Constraints
- Location registry loads at SYS_INIT priority 96

## Acceptance Criteria
- [ ] [native_sim] Integration: Location add returns unique ID
- [ ] [native_sim] Integration: Location update changes name
- [ ] [native_sim] Integration: Location delete removes entry
- [ ] [native_sim] Integration: Locations persist across reboot
- [ ] [native_sim] Integration: GET /api/locations returns JSON list
- [ ] [native_sim] Integration: Display cards show location names
- [ ] [native_sim] Integration: MQTT topics include location names
- [ ] [renode] Integration: Location persistence over simulated storage
- [ ] [manual] Board: Location CRUD via shell commands

## Related ADRs
- ADR-008: Kconfig-Only App Composition (SYS_INIT priority ordering)
- ADR-002: zbus as System-Wide Communication Fabric

## Related Zephyr subsystems
- CONFIG_SETTINGS
- CONFIG_SETTINGS_RUNTIME
- CONFIG_ZBUS
