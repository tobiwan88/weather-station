# native_sim Development Target

> Design rationale: [decisions](../decisions.md).

`native_sim` compiles the full Zephyr application as a native Linux executable. It runs on any x86_64 or ARM64 Linux host (including GitHub Actions runners and the devcontainer). Zephyr threads, timers, zbus, logging, and the shell all work correctly with no emulator, no QEMU, and no hardware.

v1 of the project targets `native_sim` exclusively. Development targets native_sim; hardware targets (frdm_mcxn947, lora_e5_mini, wio_e5_mini) build in CI.

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
| LoRa radio | Fake driver (no PHY); node->gateway is exercised over the FIFO pipe transport |
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

## native_sim + sensor node (FIFO)

On native_sim the node->gateway path uses `pipe_publisher` (`apps/sensor-node`) and `pipe_transport` (gateway), joined by a POSIX FIFO. The gateway opens the FIFO non-blocking, so it also boots without a node. The LoRa radio uses a fake driver (`lora_drv_fake_zephyr.c`) with no PHY simulation. Integration test: `tests/integration/pytest/test_sensor_node_gateway.py` (needs `SENSOR_NODE_EXE`).

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
