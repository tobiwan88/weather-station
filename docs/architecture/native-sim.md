# native_sim Development Target

> Design rationale: [ADR-009](../adr/ADR-009-native-sim-first.md).

`native_sim` compiles the full Zephyr application as a native Linux executable. It runs on any x86_64 or ARM64 Linux host (including GitHub Actions runners and the devcontainer). Zephyr threads, timers, zbus, logging, and the shell all work correctly with no emulator, no QEMU, and no hardware.

v1 of the project targets `native_sim` exclusively. No real hardware build targets are committed until the architecture is validated end-to-end in simulation.

---

## Development progression

```
Phase 1: native_sim  ◄── CURRENT     Phase 2: Renode            Phase 3: Real hardware
─────────────────────                ─────────────────          ─────────────────────
Single binary                        Two binaries in            Real MCU boards
Full app logic                       simulated network          Full integration
Shell interaction                    Multi-node test            Flash + debug
Fast iteration                       Automated assertions       Production config
                                     (see backlog [RENODE-PHASE2])
```

---

## What works on native_sim

| Feature | native_sim support |
|---------|-------------------|
| Zephyr kernel (threads, timers, semaphores) | Full |
| zbus publish/subscribe | Full |
| Zephyr shell (via stdin/stdout) | Full |
| Zephyr logging | Full |
| Zephyr networking (via Linux TAP) | Full (with host config) |
| MQTT client | Full (connects to host Mosquitto) |
| HTTP server | Full (accessible from host browser) |
| LVGL display | SDL2 virtual display window |
| LoRa radio | Fake driver only (no PHY simulation) |
| Real I2C/SPI sensors | No hardware — use `fake_sensors` |
| GPIO buttons | Simulated via shell commands |

---

## native_sim + MQTT + Mosquitto on host

On native_sim, the networking stack connects to the host via a Linux TAP interface. This allows the gateway app to connect to a Mosquitto broker running on the developer's machine:

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

The gateway app connects to `192.0.2.1:1883` (the host) via the TAP interface.

---

## native_sim + LVGL SDL2 display

```ini
# prj.conf additions for SDL2 virtual display
CONFIG_SDL_DISPLAY=y
CONFIG_DISPLAY_SDL_DEV_NAME="SDL_0"
CONFIG_LV_Z_SDL_INPUT_EVENTS=y   # mouse/keyboard input to LVGL
```

The virtual display opens as a window on the host desktop. Buttons B1–B4 map to keyboard keys. This makes the full UI exercisable before any display hardware is connected.

---

## native_sim + LoRa (fake driver)

There is no LoRa PHY simulation on native_sim. The `lora_radio` library provides a native_sim stub that intercepts `lora_send()` and `lora_recv()` and routes packets through a FIFO or Unix socket. This allows the sensor-node and gateway to exchange LoRa packets via the host OS when run as two separate processes:

```bash
# Terminal 1: sensor node
./build/sensor_node_native_sim/zephyr/zephyr.exe --lora-fifo=/tmp/lora_channel

# Terminal 2: gateway
./build/gateway_native_sim/zephyr/zephyr.exe --lora-fifo=/tmp/lora_channel
```

When only one node is running, `lora_recv()` simply blocks forever — the gateway works fine with local fake sensors only.

---

## CI pipeline on native_sim

GitHub Actions runners are standard Linux x86_64 — native_sim binaries run natively, with no emulator setup:

```yaml
- name: Run twister (native_sim)
  run: |
    source ~/.venv/bin/activate
    west twister -p native_sim -T tests/ --inline-logs -v -N
```

No emulator setup, no QEMU, no hardware. Tests complete in seconds.
