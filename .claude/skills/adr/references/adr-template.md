# ADR Template

Use this template for the draft presented in Step 4.

```markdown
# ADR-NNN — [Title]

| Field | Value |
|-------|-------|
| **Status** | Proposed |
| **Date** | YYYY-MM-DD |
| **Deciders** | [role / person] |

---

## Context

[From 3a — forces and constraints, no code]

---

## Decision

[From 3b — choice + reasoning, no implementation details]

---

## Consequences

**Easier:** [From 3c]
**Harder:** [From 3c]
**Constrained:** [From 3c]

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
[From 3d]

---

## See also

- Current implementation: [architecture doc or lib path from 3e]
- Related ADRs: [from 3e]
```

### Example (truncated)

```markdown
# ADR-002 — Use zbus as system-wide inter-module communication bus

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2025-01-15 |
| **Deciders** | Project lead |

---

## Context

Modules need to exchange sensor data, configuration commands, and triggers
without direct function calls. Direct coupling makes testing difficult and
prevents independent module replacement.

---

## Decision

Use Zephyr's zbus subsystem as the sole inter-module communication mechanism.
All libraries communicate through zbus channels — never by calling each other's
internal functions.

---

## Consequences

**Easier:** Module isolation, independent testing, replacement of implementations.
**Harder:** Debugging message flows, latency from channel traversal.
**Constrained:** All inter-module data must fit in flat structs (no pointers, no heap).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Direct function calls | Tight coupling, hard to test independently |
| Custom message queue per module | Duplication, no standard observer pattern |
| Zephyr MQTT locally | Overkill for in-process communication |

---

## See also

- Current implementation: `lib/sensor_event/`, `lib/sensor_trigger/`
- Related ADRs: ADR-003, ADR-004
```
