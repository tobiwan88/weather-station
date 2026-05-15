# ADR-013 — MQTT Configurable

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-04-20 |
| **Deciders** | Project founder |

---

## Context

The MQTT publisher library had hardcoded defaults and only shell-based runtime configuration. Users needed the ability to enable/disable MQTT, change broker address, and update authentication credentials at runtime via both shell and the HTTP dashboard. The existing `config_cmd_chan` pattern (used by `fake_sensors` and `sntp_sync`) provided a proven decoupled approach for runtime config changes.

---

## Decision

MQTT runtime configuration is exposed through the `config_cmd_chan` pattern. The HTTP dashboard publishes `config_cmd_event` messages; `mqtt_publisher` independently subscribes as a consumer — neither module references the other. Passwords are base64-encoded before storage in the settings subsystem under the shared `config/mqtt/` namespace. Disabling MQTT stops publishing, drains the queue, and disconnects; enabling triggers reconnection. The thread remains alive on disable to avoid thread lifecycle complexity.

The shared `config/mqtt/` namespace (over the previous `mqttp/`) groups all runtime configuration under one subtree, consistent with other config subtrees.

---

## Consequences

**Easier:**
- MQTT is fully configurable via both shell and HTTP dashboard.
- Config changes take effect immediately (reconnect on broker/auth/gateway change).
- Decoupled architecture: dashboard doesn't know about MQTT consumers.
- Passwords are not stored as plain text in settings.

**Harder:**
- Existing deployments lose MQTT settings on upgrade (namespace changed from `mqttp/` to `config/mqtt/`). No migration path.
- The `config_cmd_event` union grows with every new config command type, adding overhead to all config messages.

**Constrained:**
- Base64 encoding is not encryption — passwords are trivially recoverable (proper secrets management tracked as backlog item).
- The HTTP dashboard's MQTT form accumulation is not thread-safe; this is tolerated because the HTTP server processes POSTs sequentially on native_sim.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Direct API calls from dashboard to mqtt_publisher | Creates tight coupling between HTTP and MQTT, violating ADR-002 config decoupling principle |
| Thread abort/restart on enable/disable | Unnecessarily complex; the disabled-loop approach avoids thread lifecycle issues |
| Keep `mqttp/` namespace | Inconsistent with shared `config/` namespace used by other config subtrees |
| AES-encrypted password storage | Over-engineering for a demo device; deferred to a future improvement |

---

## See also

- Current implementation: `lib/mqtt_publisher/`, `lib/config_cmd/`, `lib/http_dashboard/`
- Related ADRs: [ADR-002](ADR-002-zbus-as-system-bus.md) (zbus as system bus), [ADR-008](ADR-008-kconfig-app-composition.md) (Kconfig app composition)
