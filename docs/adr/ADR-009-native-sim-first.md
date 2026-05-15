# ADR-009 — native_sim First — No Hardware Dependency in v1

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |
| **Review trigger** | When hardware is purchased and first real build target is added |

---

## Context

Embedded IoT projects commonly block progress on hardware availability.
Developers wait for boards to arrive, flash tools to be configured, and sensor
breakouts to be wired before they can run a single line of application code.
This is especially painful when:

- Hardware choice is not yet finalised (ADR-007 defers MCU selection)
- Multiple people contribute — not everyone has hardware
- CI must run on standard Linux runners without physical devices
- AI-assisted development needs a fast feedback loop — compilation errors
  must surface in seconds, not after a 30-second flash cycle

Zephyr's `native_sim` target compiles the full Zephyr application as a native
Linux executable. It runs on any x86_64 or ARM64 Linux host (including GitHub
Actions runners and the devcontainer). Zephyr threads, timers, zbus, logging,
and the shell all work correctly. No emulator, no QEMU, no hardware.

---

## Decision

**v1 of the project targets `native_sim` exclusively.** No real hardware
build targets are committed until the architecture is validated end-to-end
in simulation.

The development progression is:

```
Phase 1: native_sim  ◄── CURRENT     Phase 2: Renode            Phase 3: Real hardware
─────────────────────                ─────────────────          ─────────────────────
Single binary                        Two binaries in            Real MCU boards
Full app logic                       simulated network          Full integration
Shell interaction                    Multi-node test            Flash + debug
Fast iteration                       Automated assertions       Production config
                                     (see backlog [RENODE-PHASE2])
```

For the feature support table, MQTT + Mosquitto setup, LVGL SDL2 configuration,
LoRa fake driver details, and CI pipeline YAML, see
[`docs/architecture/native-sim.md`](../architecture/native-sim.md).

---

## Consequences

**Easier:**
- Zero hardware cost to start contributing — laptop + Docker is sufficient.
- CI is fast (seconds per test suite) and runs on standard free-tier runners.
- Hardware choice is truly deferred — the architecture is validated before
  money is spent on chips and breakout boards.
- AI-assisted development: the agent can verify its output compiles and the
  shell commands produce expected behaviour, all within the same devcontainer.

**Harder:**
- Timing behaviour on native_sim is deterministic but not cycle-accurate.
  Real-time constraints (LoRa timing windows, I2C clock stretching) cannot
  be validated on native_sim.
- Power consumption cannot be measured or optimised on native_sim.
- SDL2 must be installed on the host or in the devcontainer for LVGL display.

**Constrained:**
- Any code that uses `k_busy_wait()` or relies on precise microsecond timing
  must be guarded with `#if !defined(CONFIG_NATIVE_SIM)` or abstracted behind
  a board-specific HAL.
- `CONFIG_FAKE_SENSORS=y` is coupled to `native_sim` by convention. CI must
  verify that a `native_sim` build without `FAKE_SENSORS` fails gracefully
  (no undefined sensor UIDs).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Buy hardware first | Blocks all development and CI; expensive iteration; hardware choice locked before architecture validated |
| QEMU instead of native_sim | native_sim is faster and simpler for Zephyr; QEMU requires machine description; native_sim has better host integration (networking, display) |
| Renode from day one | Requires MCU selection; more complex setup; Phase 2 goal after architecture proven |
| Host-native (non-Zephyr) prototype | Loses all Zephyr-specific API validation; would need to be thrown away and rewritten for real hardware |

---

## See also

- Current implementation: `apps/gateway/`, `apps/sensor-node/`, `tests/integration/`
- Architecture: [`docs/architecture/system-overview.md`](../architecture/system-overview.md), [`docs/architecture/integration-tests.md`](../architecture/integration-tests.md)
- Related ADRs: [ADR-005](ADR-005-fake-sensor-subsystem.md) (fake sensors for native_sim), [ADR-010](ADR-010-ci-and-dev-environment.md) (CI pipeline), [ADR-012](ADR-012-integration-test-architecture.md) (integration tests)
