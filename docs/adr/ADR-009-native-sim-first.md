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
Phase 1: native_sim         Phase 2: Renode            Phase 3: Real hardware
─────────────────────       ─────────────────          ─────────────────────
Single binary               Two binaries in            Real MCU boards
Full app logic              simulated network           Full integration
Shell interaction           Multi-node test             Flash + debug
Fast iteration              Automated assertions        Production config
```

### What works on native_sim without any change

| Feature | native_sim support |
|---------|-------------------|
| Zephyr kernel (threads, timers, semaphores) | ✅ Full |
| zbus publish/subscribe | ✅ Full |
| Zephyr shell (via stdin/stdout) | ✅ Full |
| Zephyr logging | ✅ Full |
| Zephyr networking (via Linux TAP) | ✅ Full (with host config) |
| MQTT client | ✅ Full (connects to host Mosquitto) |
| HTTP server | ✅ Full (accessible from host browser) |
| LVGL display | ✅ SDL2 virtual display window |
| LoRa radio | ⚠️ Fake driver only (no PHY simulation) |
| Real I2C/SPI sensors | ❌ No hardware — use fake_sensors |
| GPIO buttons | ⚠️ Simulated via shell commands |

### native_sim + MQTT + Mosquitto on host

On native_sim, the networking stack connects to the host via a Linux TAP
interface. This allows the gateway app to connect to a Mosquitto broker
running on the developer's machine:

```bash
# Host: start Mosquitto
docker run -it -p 1883:1883 eclipse-mosquitto

# Host: subscribe to watch data
mosquitto_sub -h localhost -t "weather/#" -v

# Container: configure TAP networking (done once)
sudo ip tuntap add dev zeth0 mode tap
sudo ip addr add 192.0.2.1/24 dev zeth0
sudo ip link set zeth0 up

# Container: build and run
west build -b native_sim apps/gateway
sudo ./build/zephyr/zephyr.exe
```

The gateway app connects to `192.0.2.1:1883` (the host) via the TAP
interface. The developer sees live MQTT messages on their terminal while
interacting with the app via the Zephyr shell.

### native_sim + LVGL SDL2 display

```ini
# prj.conf additions for SDL2 virtual display
CONFIG_SDL_DISPLAY=y
CONFIG_DISPLAY_SDL_DEV_NAME="SDL_0"
CONFIG_LV_Z_SDL_INPUT_EVENTS=y   # mouse/keyboard input to LVGL
```

The virtual display opens as a window on the host desktop. Buttons B1-B4
map to keyboard keys. This makes the full UI exercisable before any display
hardware is connected.

### native_sim + LoRa (fake driver)

There is no LoRa PHY simulation on native_sim. The `lora_radio` library
provides a **native_sim stub** that:

- Intercepts `lora_send()` and writes the packet to a FIFO / Unix socket
- Intercepts `lora_recv()` and reads from the same FIFO / Unix socket

This allows the sensor-node and gateway to exchange LoRa packets via the
host OS when run as two separate processes:

```bash
# Terminal 1: sensor node
./build/sensor_node_native_sim/zephyr/zephyr.exe --lora-fifo=/tmp/lora_channel

# Terminal 2: gateway
./build/gateway_native_sim/zephyr/zephyr.exe --lora-fifo=/tmp/lora_channel
```

When only one node is running, `lora_recv()` simply blocks forever — the
gateway works fine with local fake sensors only.

### CI pipeline on native_sim

GitHub Actions runners are standard Linux x86_64 — native_sim binaries run
natively:

```yaml
- name: Run twister (native_sim)
  run: |
    source ~/.venv/bin/activate
    west twister -p native_sim -T tests/ --inline-logs -v -N
```

No emulator setup, no QEMU, no hardware. Tests complete in seconds.

### Transition to Renode (Phase 2)

Renode simulates multiple Zephyr nodes with a virtual radio medium. When the
LoRa stub on native_sim is replaced with a proper Renode platform description,
end-to-end multi-node tests run automatically in CI:

```
# simulation/multi_node.resc
mach create "sensor_node"
machine LoadPlatformDescription @nrf52840.repl
sysbus LoadELF @artifacts/sensor_node.elf

mach create "gateway"
machine LoadPlatformDescription @nrf52840.repl
sysbus LoadELF @artifacts/gateway.elf

emulation CreateWirelessMedium "lora_medium"
# ... connect radios to medium
```

The Renode phase requires MCU selection (ADR-007 trigger) and is not in scope
for v1.

### Transition to real hardware (Phase 3)

Adding a real hardware target requires:
1. Write `boards/<vendor>/<board>/` board definition (or use an upstream one).
2. Write `apps/gateway/boards/<board>.overlay` and `<board>.conf`.
3. Set `CONFIG_FAKE_SENSORS=n`, `CONFIG_BME280=y`, `CONFIG_I2C=y`, etc.
4. Add a new row to the CI build matrix.

Application source code (`lib/`, `apps/*/src/`) is unchanged.

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
