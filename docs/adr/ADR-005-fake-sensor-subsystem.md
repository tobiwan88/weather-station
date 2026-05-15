# ADR-005 — Fake Sensor Subsystem for native_sim

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

The project targets `native_sim` before any hardware is purchased or selected.
This means no real I2C sensors, no SPI LoRa modules, no physical buttons.
However, the full application logic — trigger-driven sampling, zbus routing,
display update, MQTT publish — must be exercisable without hardware.

Options for providing sensor data in simulation:

1. **Hardcoded constant values in `main.c`** — Simple but untestable and
   non-interactive. Can't simulate outdoor temperature going negative, or
   a spike in CO2 to verify display colour changes.

2. **Zephyr emulator framework** (`CONFIG_EMUL=y`) — Emulates I2C/SPI bus
   transactions at the driver level. Requires implementing the sensor register
   map. Correct but very low-level for this use case.

3. **`native_sim` fake drivers that implement the same trigger/publish pattern
   as real drivers, with shell-settable values** — Interactive, realistic,
   and directly tests the application logic path.

The choice also has a significant impact on **AI-assisted development**: when
asking Claude to write a new feature, the fake sensor subsystem must be
self-documenting and mechanical. A new contributor (or AI agent) should be
able to add a new fake sensor type by reading one existing fake driver and
copying the pattern — with no hidden wiring.

---

## Decision

Implement a **fake sensor subsystem** as a first-class, production-quality
library module in `lib/fake_sensors/`. It is not a test stub — it is the
primary sensor backend for `native_sim` development.

### Key properties

1. **Devicetree-defined.** Fake sensors are declared in board overlays using
   custom `compatible` strings. The Zephyr build system discovers them
   automatically when `CONFIG_FAKE_SENSORS=y`.

2. **Shell-interactive.** Values are set via `fake_sensors <type>_set <uid> <value>`
   shell commands. This allows live manipulation of sensor readings during
   a running simulation.

3. **Trigger-driven.** Fake sensors subscribe to `sensor_trigger_chan` and
   publish to `sensor_event_chan` — identical behaviour to real sensor drivers.
   From the perspective of consumers (display, MQTT), fake and real sensors
   are indistinguishable.

4. **Self-registering.** Each fake driver instance registers itself into the
   fake sensor subsystem via `STRUCT_SECTION_ITERABLE`. The shell commands
   enumerate all registered instances — no central list to maintain.

5. **Swap to real hardware by Kconfig only** — `CONFIG_FAKE_SENSORS=n` plus
   `CONFIG_BME280=y` in a board `.conf`; no source code changes.

For the module structure, DT binding format, STRUCT_SECTION_ITERABLE registration
macro, shell interaction transcript, auto-publish configuration, and data flow
diagram, see [`docs/architecture/fake-sensors.md`](../architecture/fake-sensors.md).

---

## Consequences

**Easier:**
- Full application logic is exercisable from day one — no hardware required.
- Shell-settable values enable manual and automated testing of edge cases
  (below-freezing outdoor temp, 100% humidity, battery low voltage).
- Adding a new fake sensor type requires one new file (`fake_co2.c`) and one
  new DT binding YAML — zero changes to existing code.
- CI tests can use `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS` to exercise the full
  pipeline automatically.

**Harder:**
- The fake sensor values are not realistic — they don't drift, they don't have
  noise. Realism requires either shell scripting or future enhancement with a
  configurable noise/drift model.
- Developers must remember to set `CONFIG_FAKE_SENSORS=n` when building for
  real hardware. Kconfig default is `n` specifically to prevent accidental
  inclusion.

**Constrained:**
- `CONFIG_FAKE_SENSORS` must never be `y` in a production/release build.
  Enforce this with a Kconfig `depends on !RELEASE` guard or CI check.
- Fake sensor UIDs must be reserved and not reused for real hardware sensors.
  Convention: fake sensors use UIDs 0x0001–0x00FF; real hardware starts at 0x0100.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Hardcoded constants in `main.c` | Not interactive; can't test edge cases; not representative of real data flow |
| Zephyr I2C emulator (`CONFIG_EMUL=y`) | Correct at the protocol level but requires implementing sensor register maps; much more code for the same testing benefit |
| Conditional `#ifdef` in real drivers | Pollutes production code with simulation logic; not easily extensible; violates separation of concerns |
| Python host-side value injection via UART | Extra tooling dependency; delays; doesn't test the actual sensor driver path |
| No simulation — buy hardware first | Blocks development; expensive iteration; CI impossible without hardware farm |

---

## See also

- Current implementation: `lib/fake_sensors/`, `dts/bindings/fake,temperature.yaml`
- Related ADRs: [ADR-003](ADR-003-sensor-event-data-model.md) (event struct), [ADR-004](ADR-004-trigger-driven-sampling.md) (trigger pattern), [ADR-009](ADR-009-native-sim-first.md) (native_sim first)
