# ADR-013. MQTT Configurable

**Date:** 2026-04-20
**Status:** Accepted

## Context

The MQTT publisher library had hardcoded defaults and only shell-based runtime configuration. Users needed the ability to enable/disable MQTT, change broker address, and update authentication credentials at runtime via both shell and the HTTP dashboard.

## Decision

MQTT runtime configuration is exposed through the decoupled `config_cmd_chan` pattern (Option A). The HTTP dashboard publishes `config_cmd_event` messages; `mqtt_publisher` independently subscribes as a consumer. Passwords are base64-encoded before storage in the settings subsystem under the shared `config/mqtt/` namespace. Disabling MQTT stops publishing, drains the queue, and disconnects; enabling triggers reconnection.

## Rationale

- **Option A (config_cmd_chan)** keeps the HTTP dashboard decoupled from MQTT internals, consistent with how `fake_sensors` and `sntp_sync` handle config changes.
- **Shared namespace** (`config/mqtt/`) groups all runtime configuration under one subtree for consistency.
- **Base64 encoding** provides minimal obfuscation for passwords in settings storage (flash is not encrypted). A proper secrets management approach is tracked as a backlog item.
- **Stop-publishing-on-disable** avoids thread lifecycle complexity; the thread remains alive but drops events and sleeps in a disabled loop.

## Alternatives Considered

- **Option B (direct API calls from dashboard)** — rejected because it creates tight coupling between HTTP and MQTT, violating the config decoupling principle established in ADR-002.
- **Thread abort/restart on enable/disable** — rejected as unnecessarily complex; the disabled-loop approach is simpler and avoids thread lifecycle issues.
- **Keep `mqttp/` namespace** — rejected in favor of the shared `config/` namespace for consistency with other config subtrees.
- **AES-encrypted password storage** — rejected as over-engineering for a demo device; deferred to a future improvement.

## Consequences

### Positive
- MQTT is fully configurable via both shell and HTTP dashboard.
- Config changes take effect immediately (reconnect on broker/auth/gateway change).
- Decoupled architecture: dashboard doesn't know about MQTT consumers.
- Password is not stored as plain text in settings.

### Negative / Trade-offs
- Existing deployments lose MQTT settings on upgrade (namespace changed from `mqttp/` to `config/mqtt/`). No migration path.
- Base64 encoding is not encryption — passwords are trivially recoverable.
- The `config_cmd_event` union adds ~150 bytes to every config event (broker struct is 68 bytes, auth struct is 96 bytes).
- Static variables in `process_post.c` for MQTT form accumulation are not thread-safe, but the HTTP server processes POSTs sequentially on native_sim.

## Related

- `lib/mqtt_publisher/` — MQTT publisher library
- `lib/config_cmd/` — config command channel
- `lib/http_dashboard/` — HTTP dashboard
- ADR-002 — zbus as system bus
- ADR-008 — Kconfig app composition
