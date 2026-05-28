# REQ-MQTT — MQTT Publisher Requirements

## Status
review

## Version
0.1

## Context
MQTT publishes sensor data to an external broker for cloud integration and remote monitoring. The MQTT publisher is fully configurable at runtime via the config_cmd channel, supports broker authentication, and implements self-healing reconnection.

## Functional Requirements

### REQ-MQTT-001 — Broker Authentication
The system shall support username/password authentication for MQTT broker connections. Credentials shall be base64-encoded and stored in settings under `config/mqtt/`.

### REQ-MQTT-002 — Topic Hierarchy
The system shall publish to topics following the pattern: `{gateway_id}/{location}/{sensor_name}/{measurement_type}`.

### REQ-MQTT-003 — QoS Support
The system shall support configurable QoS levels (0, 1, 2) for MQTT publish operations.

### REQ-MQTT-004 — Runtime Reconfiguration
The system shall accept MQTT configuration changes (broker address, port, credentials, gateway ID) via `config_cmd_chan` at runtime. Changes shall take effect immediately with reconnection.

### REQ-MQTT-005 — Self-Healing Reconnect
The system shall automatically reconnect to the MQTT broker on connection loss, with exponential backoff.

### REQ-MQTT-006 — Publish on Sensor Events
The system shall subscribe to `sensor_event_chan` and publish each sensor measurement to the MQTT broker using the topic hierarchy from REQ-MQTT-002.

### REQ-MQTT-007 — Configurable Enable/Disable
The system shall support enabling and disabling MQTT publishing at runtime via `config_cmd_chan`. On disable, the system shall stop publishing, drain the publish queue, and disconnect from the broker.

### REQ-MQTT-008 — Settings Persistence
The system shall persist MQTT configuration across reboots using Zephyr settings subsystem under the `config/mqtt/` namespace.

## Dependencies
- REQ-CONFIG-001: config_cmd channel contract
- REQ-SENSORS-001: sensor event data model
- REQ-LOCATION-001: named location registry

## Constraints
- Base64 encoding is not encryption — passwords are trivially recoverable
- HTTP dashboard MQTT form is not thread-safe (tolerated on native_sim single-threaded HTTP)
- Thread remains alive on disable to avoid thread lifecycle complexity
- Existing deployments lose settings on upgrade (namespace changed from `mqttp/` to `config/mqtt/`)

## Acceptance Criteria
- [ ] [native_sim] Integration: MQTT publisher connects to broker with username/password
- [ ] [native_sim] Integration: Sensor events published to correct topic hierarchy
- [ ] [native_sim] Integration: Runtime config change triggers reconnection
- [ ] [native_sim] Integration: Disable MQTT stops publishing and disconnects
- [ ] [native_sim] Integration: Reconnect succeeds after broker restart
- [ ] [renode] Integration: MQTT publish over simulated network
- [ ] [manual] Board: MQTT publishes to external broker (Mosquitto/AWS IoT)

## Related ADRs
- ADR-013: MQTT Configurable
- ADR-002: zbus as System-Wide Communication Fabric
- ADR-008: Kconfig-Only App Composition

## Related Zephyr subsystems
- CONFIG_MQTT_LIB
- CONFIG_NET_SOCKETS
- CONFIG_NET_TCP
- CONFIG_SETTINGS
- CONFIG_TLS_CREDENTIALS
