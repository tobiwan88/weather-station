# REQ-CONFIG — Configuration Management Requirements

## Status
review

## Version
0.1

## Context
Runtime configuration is managed through the `config_cmd` zbus channel, supporting commands for MQTT, SNTP, locations, and other subsystems. Configuration persists across reboots via Zephyr settings.

## Functional Requirements

### REQ-CONFIG-001 — Config Command Channel
The system shall provide a `config_cmd_chan` zbus channel for runtime configuration commands. Producers publish config commands; consumers (MQTT, SNTP, location, etc.) subscribe and apply changes.

### REQ-CONFIG-002 — Supported Commands
The system shall support the following configuration commands:
- MQTT: broker address, port, username, password, gateway ID, enable/disable
- SNTP: resync request
- Location: add, update, delete, list
- Other subsystems as needed

### REQ-CONFIG-003 — Settings Persistence
The system shall persist configuration values across reboots using Zephyr settings subsystem. Each subsystem shall use its own settings namespace (e.g., `config/mqtt/`, `config/location/`).

### REQ-CONFIG-004 — Shell Interface Parity
The system shall provide shell commands that mirror the config_cmd channel functionality, enabling configuration via UART/console without HTTP dashboard.

### REQ-CONFIG-005 — Immediate Effect
The system shall apply configuration changes immediately upon receipt. MQTT changes shall trigger reconnection; SNTP resync shall trigger immediate query; location changes shall update the registry.

### REQ-CONFIG-006 — Kconfig Composition
The system shall be composed via Kconfig — each subsystem's config support shall be gated by its own `CONFIG_*` symbol. App CMakeLists.txt shall contain no feature-selection logic.

## Dependencies
- REQ-MQTT-004: MQTT runtime reconfiguration
- REQ-MQTT-008: MQTT settings persistence
- REQ-LOCATION-001: Location registry
- REQ-TIME-003: SNTP resync via config command

## Constraints
- zbus listeners must not block (config consumers must defer heavy work)
- Base64 encoding for MQTT password is not encryption
- Existing deployments lose settings on namespace changes

## Acceptance Criteria
- [ ] [native_sim] Integration: config_cmd channel delivers commands to subscribers
- [ ] [native_sim] Integration: MQTT config change triggers reconnection
- [ ] [native_sim] Integration: Location add/update/delete persists across reboot
- [ ] [native_sim] Integration: Shell commands mirror config_cmd functionality
- [ ] [native_sim] Integration: SNTP resync via config_cmd triggers immediate query
- [ ] [native_sim] ztest: Config command struct is copyable (no heap pointers)
- [ ] [renode] Integration: Config persistence over simulated storage
- [ ] [manual] Board: Shell configuration via UART console

## Related ADRs
- ADR-008: Kconfig-Only App Composition
- ADR-013: MQTT Configurable
- ADR-002: zbus as System-Wide Communication Fabric

## Related Zephyr subsystems
- CONFIG_SETTINGS
- CONFIG_SETTINGS_RUNTIME
- CONFIG_SHELL
- CONFIG_ZBUS
