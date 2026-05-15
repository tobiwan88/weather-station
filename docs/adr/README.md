# Architecture Decision Records

This directory contains the Architecture Decision Records (ADRs) for the
`weather-station` project. ADRs document **why** the system is designed the
way it is — not just what was decided, but the context and consequences that
shaped each choice.

Use these documents when:
- Onboarding new contributors
- Guiding an AI coding agent (include relevant ADRs in context)
- Revisiting a decision when requirements change

---

## Index

| ADR | Title | Status |
|-----|-------|--------|
| [ADR-001](ADR-001-repo-and-workspace-structure.md) | Repository and West Workspace Structure | Accepted |
| [ADR-002](ADR-002-zbus-as-system-bus.md) | zbus as the System-Wide Communication Fabric | Accepted |
| [ADR-003](ADR-003-sensor-event-data-model.md) | Sensor Event Data Model (flat struct + Q31) | Accepted |
| [ADR-004](ADR-004-trigger-driven-sampling.md) | Trigger-Driven Sensor Sampling (no sensor manager) | Accepted |
| [ADR-005](ADR-005-fake-sensor-subsystem.md) | Fake Sensor Subsystem for native_sim | Accepted |
| [ADR-006](ADR-006-lora-channel-boundary.md) | LoRa Module Channel Boundary | Accepted |
| [ADR-007](ADR-007-gateway-display-combined.md) | Gateway and Display as One Device (initial phase) | Accepted |
| [ADR-008](ADR-008-kconfig-app-composition.md) | Kconfig-Only App Composition | Accepted |
| [ADR-009](ADR-009-native-sim-first.md) | native_sim First — No Hardware Dependency in v1 | Accepted |
| [ADR-010](ADR-010-ci-and-dev-environment.md) | CI/CD and Developer Environment | Accepted |
| [ADR-011](ADR-011-http-dashboard.md) | HTTP Dashboard Web Interface | Accepted |
| [ADR-012](ADR-012-integration-test-architecture.md) | Pytest Integration Test Architecture | Accepted |
| [ADR-013](ADR-013-mqtt-configurable.md) | MQTT Configurable | Accepted |
| [ADR-014](ADR-014-mcuboot-fota.md) | MCUboot FOTA and Secure Firmware Update | Accepted |
| [ADR-015](ADR-015-lora-protocol.md) | LoRa Protocol Architecture | Accepted |

---

## ADR format

Each ADR uses this canonical structure:

```markdown
# ADR-NNN — Title

| Field | Value |
|-------|-------|
| **Status** | Proposed / Accepted / Deprecated / Superseded by ADR-NNN |
| **Date** | YYYY-MM-DD |
| **Deciders** | Role or person |

---

## Context

The forces, constraints, and problem statement that required a decision.
(WHY is a decision needed here? No implementation details.)

---

## Decision

What was decided. State the choice clearly.
Do NOT include struct layouts, enum tables, or uid ranges here — reference them.

---

## Consequences

**Easier:** ...
**Harder:** ...
**Constrained:** ...

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Option B    | ... |

---

## See also

- Current implementation: `docs/architecture/<doc>.md`
- Key types: `lib/<library>/include/...`
- Related ADRs: ADR-NNN, ADR-NNN
```

### Decision threshold

Write a new ADR for **architectural decisions** only: new inter-library protocol, new zbus channel, new Kconfig composition rule, new test harness, significant trade-off with long-term consequences. Skip for helper functions, naming, minor config defaults, or routine refactors.

### Write-once rule

ADRs are **write-once**. After Accepted, only the Status field changes. To supersede a decision, create a new ADR and set the old ADR's Status to `Superseded by ADR-NNN`. Never edit the Context, Decision, or Consequences of an Accepted ADR.

### See also section

Every ADR links forward to its implementation: the architecture doc that describes HOW, and the header or source file that defines the key types. This keeps ADRs short (WHY only) without becoming dead ends.

---

## How to use with an AI coding agent

When asking Claude (or another agent) to implement a feature, include the
relevant ADR(s) in the prompt context. Example:

```
Context: See ADR-003 (data model) and ADR-004 (trigger pattern)
before writing any sensor driver code.
```

This prevents the agent from reverting to common but wrong patterns
(e.g. a central sensor manager, polling in a driver thread, float in events).
