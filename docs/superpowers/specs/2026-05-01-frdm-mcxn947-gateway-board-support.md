# FRDM-MCXN947 Gateway Board Support — Design Spec

**Date:** 2026-05-01
**Status:** Draft

## Goal

Add FRDM-MCXN947 as the first real hardware target for the gateway application, while keeping fake (simulated) sensors. The board boots, runs the full gateway stack (shell, zbus, HTTP dashboard, MQTT, SNTP, settings), and samples fake sensors — all on real ARM Cortex-M33 hardware with real Ethernet networking.

## Architecture

Two new files under `apps/gateway/boards/`:

### `frdm_mcxn947.overlay`

DeviceTree overlay defining the same 6 fake sensor nodes as `native_sim.overlay`:

| Node | Compatible | UID | Initial Value |
|---|---|---|---|
| `fake-temp-indoor` | `fake,temperature` | `0x0001` | 21.0 °C |
| `fake-hum-indoor` | `fake,humidity` | `0x0002` | 50.0 % |
| `fake-temp-outdoor` | `fake,temperature` | `0x0011` | 4.0 °C |
| `fake-hum-outdoor` | `fake,humidity` | `0x0012` | 30.0 % |
| `fake-co2-indoor` | `fake,co2` | `0x0003` | 800 ppm |
| `fake-voc-indoor` | `fake,voc` | `0x0004` | 250 IAQ |

No SDL display section — the board has no graphical display.

### `frdm_mcxn947.conf`

Kconfig fragment applied after `prj.conf`. It overrides two subsystems:

**1. Replace native offloaded sockets with real Ethernet:**
- `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=n`
- `CONFIG_NET_NATIVE_OFFLOAD=n`
- `CONFIG_ETH_MCUX=y` — MCUX Ethernet driver for the on-board PHY
- `CONFIG_NET_IPV4=y`
- `CONFIG_NET_DHCPV4=y` — obtain IP via DHCP
- `CONFIG_NET_CONFIG_AUTO_INIT=y`

**2. Disable display stack (hardware not present):**
- `CONFIG_LVGL_DISPLAY=n`
- `CONFIG_LVGL=n`
- `CONFIG_DISPLAY=n`
- `CONFIG_INPUT=n`

**3. Memory tuning for 512 KB RAM:**
- `CONFIG_HEAP_MEM_POOL_SIZE=16384` (down from 32768)
- `CONFIG_MAIN_STACK_SIZE=2048`
- `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048`

All other features remain enabled via `prj.conf`: shell, zbus, fake sensors, settings, HTTP dashboard (port 8080), MQTT, SNTP, location registry, clock display.

### `main.c` — No changes

Already guarded:
```c
#if CONFIG_LVGL_DISPLAY
    lvgl_display_run();
#else
    k_sleep(K_FOREVER);
#endif
```

On `frdm_mcxn947`, `CONFIG_LVGL_DISPLAY=n` so it falls through to `k_sleep(K_FOREVER)`.

## Build Command

```bash
west build apps/gateway -b frdm_mcxn947
```

## Scope Boundaries

**In scope:**
- Gateway builds and boots on FRDM-MCXN947
- Fake sensors publish events via zbus
- Shell accessible over UART
- HTTP dashboard on port 8080 via Ethernet
- MQTT publisher via Ethernet
- SNTP sync via Ethernet

**Out of scope (future work):**
- Real hardware sensor drivers
- Integration tests on hardware
- Wi-Fi (board supports it via shield, not on-board)
- Display/LVGL on hardware

## Risks

1. **Memory pressure** — 512 KB RAM must hold the full gateway stack (HTTP server, MQTT, zbus, settings, shell). Stack sizes and heap may need further tuning after first boot.
2. **Ethernet PHY configuration** — The on-board PHY must be correctly enabled in the board's base DTS. Zephyr's `frdm_mcxn947` board definition should handle this, but may require overlay tweaks if the default DTS disables it.
3. **HTTP server memory** — `CONFIG_HTTP_SERVER_*` buffer sizes tuned for native_sim may need reduction.
