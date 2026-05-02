# ADR-002 — zbus as the System-Wide Communication Fabric

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

The weather station firmware has multiple independent producers and consumers of data that must communicate without direct coupling. Without a structured communication mechanism, modules would reference each other directly, creating tight coupling, untestable code, and no clear boundary between layers.

For the full rationale and channel listing, see [`docs/architecture/event-bus.md`](../architecture/event-bus.md).

---

## Decision

Use **Zephyr's zbus** as the system-wide publish/subscribe fabric.

### Core rules

1. **One concern = one channel.** Channels are never reused for different data types.

2. **Channels are declared in their library's public header** (`ZBUS_CHAN_DECLARE`) **and defined in exactly one `.c` file** (`ZBUS_CHAN_DEFINE`). No channel is defined in a shared `include/common/` directory.

3. **Producers never know who their consumers are.** A sensor driver calls `zbus_chan_pub()` and returns. It has no knowledge of what is listening.

4. **Consumers choose their subscription model:**
   - **Listener** — synchronous callback in publisher's context; must not block or sleep.
   - **Subscriber** — thread-based with a queue; for work that takes time (MQTT publish, network I/O).

5. **Message structs must be copyable** (no heap pointers). This is enforced by the flat `env_sensor_data` design (see ADR-003).

### Channel map (current)

| Channel | Owner | Direction |
|---|---|---|
| `sensor_trigger_chan` | `lib/sensor_trigger` | trigger sources → sensor drivers |
| `sensor_event_chan` | `lib/sensor_event` | sensor drivers → consumers |
| `config_cmd_chan` | `lib/config_cmd` | config producers → config consumers |
| `remote_scan_ctrl_chan` | `lib/remote_sensor` | manager/shell → transport adapters |

Remote sensor discovery uses a `k_msgq` (not zbus) inside `remote_sensor_manager` for ordering guarantees. See `docs/architecture/event-bus.md` for details.

---

## Consequences

**Easier:**
- Adding a new consumer = register a subscriber. Zero changes to any producer.
- Unit testing a producer = publish to channel, assert subscriber received correct message.
- Extending to new sensor types = same channel, new `enum sensor_type` value.

**Harder:**
- Debugging message flow requires understanding zbus observer ordering. The `zbus shell` command helps.
- Subscriber queue depth must be tuned. Too small = missed messages under load.

**Constrained:**
- Listeners run in the publisher's thread context — must not block or sleep. Heavy work must use subscriber threads.
- Message structs must be copyable (no pointers to heap).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Direct function calls between modules | Tight coupling; producer must know all consumers; untestable in isolation |
| Zephyr message queues (`k_msgq`) | Point-to-point only; fan-out to multiple consumers requires manual multiplexing |
| Custom callback registry | Reinvents zbus poorly; no tooling, no shell introspection |
| MQTT internally (on-device) | Absurd overhead; designed for network transport not intra-MCU IPC |
