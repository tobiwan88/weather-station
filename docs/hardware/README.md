# Hardware

This document covers hardware setup and target board information for the Weather Station project.

## Supported Targets

### native_sim/native/64 (Primary)

The primary development and testing target. Runs on the host machine as native POSIX threads.

- **No hardware required** — everything runs on the host
- **Display:** SDL window (320×240) via Xvfb in devcontainer
- **Network:** Host network stack (HTTP, MQTT, SNTP all use real sockets)
- **Sensors:** Fake sensor drivers instantiated via device tree

Build command:
```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -b native_sim/native/64 apps/gateway
```

Binary output: `build/native_sim_native_64/gateway/zephyr/zephyr.exe`

### FRDM-MCXN947

NXP FRDM-MCXN947 development board support. See the board support spec for details.

### Future Targets

Additional hardware targets can be added by creating a board overlay and defconfig under `apps/gateway/boards/`.

## Hardware Setup (Placeholder)

*This section will evolve as hardware-specific setup instructions are added.*

### Requirements

- Target board (see supported targets above)
- USB cable for serial console and flashing
- Network connectivity (Ethernet or Wi-Fi, depending on board)
- Optional: MQTT broker on the local network for sensor data publishing

### Flashing

*Flashing instructions will be added per target board.*

### Serial Console

*Serial console configuration and access instructions will be added per target board.*

## Device Tree

Sensor instances are defined in board overlay files (`.overlay`). Each sensor node specifies:
- Compatible driver (e.g., `fake-temp-sensor`, `fake-humidity-sensor`)
- Status (`okay` to enable)
- Sensor UID (unique identifier, see UID allocation ranges below)

### Sensor UID Allocation

| Range | Purpose |
|---|---|
| `0x0001–0x000F` | Gateway-local / indoor sensors |
| `0x0011–0x001F` | Gateway outdoor sensors |
| `0x0021–0x00FF` | Remote sensor nodes |
| `0x0101+` | Test-only instances |

UIDs must be unique across all overlay files — they are the identity key for the sensor registry, LVGL display, and MQTT topics.
