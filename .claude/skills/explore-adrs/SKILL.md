---
name: explore-adrs
description: Discover and present relevant ADRs before starting feature work. Reads the ADR index, matches decisions to the feature request, and surfaces them as design constraints. Invoke before brainstorming or any implementation work.
disable-model-invocation: false
---

# Explore ADRs

Before starting any feature work, discover which Architecture Decision Records (ADRs) constrain the design. This ensures implementation follows established patterns and flags gaps where new ADRs are needed.

## When to use

- Before brainstorming a new feature (invoke before or during the "Explore project context" step)
- Before implementing any feature when the user hasn't provided ADR context
- After the user describes a feature request and before asking clarifying questions

## Procedure

### 1. Read the ADR index

```bash
cat docs/adr/README.md
```

If the index doesn't exist, check for `doc/adr/` or `adr/`. If none exist, report: "No ADR index found — this project may not use ADRs yet."

### 2. Match ADRs to the feature request

Read the ADR index table and identify which ADRs are relevant to the feature. Consider:

| Feature topic | Likely relevant ADRs |
|---|---|
| New sensor driver | ADR-003 (data model), ADR-004 (trigger pattern), ADR-005 (fake sensors) |
| New library/service | ADR-001 (structure), ADR-002 (zbus), ADR-008 (Kconfig composition) |
| UI/display changes | ADR-007 (gateway+display), ADR-011 (HTTP dashboard) |
| Connectivity (MQTT, HTTP, LoRa) | ADR-002 (zbus), ADR-006 (LoRa), ADR-013 (MQTT) |
| Testing changes | ADR-012 (integration tests), ADR-009 (native_sim) |
| CI/developer environment | ADR-010 (CI/dev env) |
| Configuration/settings | ADR-008 (Kconfig), ADR-013 (MQTT configurable) |

### 3. Read the matched ADRs

Read the full content of each matched ADR file. Extract:
- The **Decision** (what was decided)
- Key **Consequences** (constraints that affect the new feature)
- Any **Related** ADRs not yet in the matched set

### 4. Present ADRs as design constraints

Present the matched ADRs to the user in this format:

```
Relevant ADRs for this feature:

- **ADR-XXX** — [Title]: [Decision in one sentence]
  Constraint: [What this means for the current feature]

- **ADR-YYY** — [Title]: [Decision in one sentence]
  Constraint: [What this means for the current feature]
```

### 5. Flag missing ADRs

If a key aspect of the feature request has no matching ADR, flag it:

> "There's no ADR covering [topic]. Should we create one before proceeding?"

This is especially important when:
- The feature introduces a new communication pattern
- The feature adds a new type of library or service
- The feature changes system-level structure
- The feature introduces a new trade-off not covered by existing ADRs

## Output

Include the ADR references in any spec or design document produced after this step. Example:

```
Constrained by ADR-003 (flat sensor events), ADR-004 (trigger-driven sampling),
and ADR-008 (Kconfig-only composition).
```
