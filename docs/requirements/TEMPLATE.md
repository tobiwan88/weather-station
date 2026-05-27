# REQ-TEMPLATE — Requirements Template

## Status
draft | review | approved | deprecated

## Version
0.1

## Context
Why this feature/constraint exists.

## Functional Requirements

### REQ-TEMPLATE-001 — Title
The system shall [verb] [object] [condition].

### REQ-TEMPLATE-002 — Title
The system shall [verb] [object] [condition].

## Dependencies
- REQ-DOMAIN-NNN: [reason]
- (or "none yet")

## Constraints
- Power: ≤ [X] µA average during [mode]
- Latency: ≤ [X] ms [from event to output]
- Memory: ≤ [X] KB [SRAM/flash]

## Acceptance Criteria
Each criterion tagged with how it can be verified:

- [ ] [native_sim] ztest: [description]
- [ ] [renode] Integration: [description]
- [ ] [hil] Power test: [description]
- [ ] [manual] Board: [description]

## Related ADRs
- ADR-NNNN (if exists, else "none yet")

## Related Zephyr subsystems
- CONFIG_*, etc.
