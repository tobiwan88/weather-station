# Design: `apps/lora_bridge` — Wio-E5 Mini LoRa Co-processor Firmware

**Date:** 2026-05-16
**Status:** Draft
**Backlog:** `LORA-BRIDGE-APP`

## Overview

The gateway's LoRa radio runs on a dedicated Wio-E5 Mini (STM32WLE5JC) co-processor
connected via UART (Flexcomm5 on the MCXN947). This app runs `lib/lora_radio/` on
the co-processor and forwards decoded `env_sensor_data` frames to the gateway over UART.

## Architecture

```
┌───────────────────────── Wio-E5 Mini (lora_bridge app) ─────────────────────────┐
│                                                                                  │
│  lib/lora_radio/                     lib/uart_lora_sender/   (NEW)               │
│  ┌──────────────────────┐           ┌──────────────────────────┐                 │
│  │ LoRa RX → decode →   │──zbus──▶  │ Subscribe sensor_event_  │                 │
│  │ publish to            │           │ chan → pack to 24-byte   │──UART──▶Gateway │
│  │ sensor_event_chan     │           │ wire → uart_poll_out()   │   USART1       │
│  └──────────────────────┘           └──────────────────────────┘                 │
│                                                                                  │
│  zbus: sensor_trigger_chan, sensor_event_chan                                    │
│  Radio: SX126x @ 868 MHz, SF10, BW125                                            │
└──────────────────────────────────────────────────────────────────────────────────┘
```

Gateway side (already exists): `lib/uart_lora_bridge/` receives 24-byte frames,
converts to `env_sensor_data`, publishes to gateway's `sensor_event_chan`.

## Components

### 1. `boards/wio_e5_mini/` — Custom Zephyr Board Definition

Workspace root `boards/` directory. Adapted from upstream `nucleo_wl55jc`.

| File | Purpose |
|------|---------|
| `wio_e5_mini.dts` | DTS: includes `stm32wl55Xc.dtsi`, `wio_e5_mini-pinctrl.dtsi`; SubGHz SPI with `lora: radio@0`; USART1 for gateway UART; RF switch GPIOs PA4/PA5; LED PB5; I2C2 (future sensors) |
| `wio_e5_mini_defconfig` | `CONFIG_SERIAL=y`, `CONFIG_GPIO=y`, `CONFIG_CONSOLE=y`, `CONFIG_UART_CONSOLE=y`, `CONFIG_ARM_MPU=y`, `CONFIG_HW_STACK_PROTECTION=y` |
| `Kconfig.wio_e5_mini` | `BOARD_WIO_E5_MINI` → selects `SOC_STM32WL55XX` |
| `board.cmake` | Runners: openocd-stm32, stm32cubeprogrammer |
| `wio_e5_mini-pinctrl.dtsi` | Pinctrl: `usart1_tx_pb6_pb6`, `usart1_rx_pb7_pb7`, RF switch on PA4/PA5 |

**Key pin differences vs NUCLEO-WL55JC:**

| Function | NUCLEO-WL55JC | Wio-E5 Mini |
|----------|--------------|-------------|
| RF_CTRL1 | PC4 | PA4 |
| RF_CTRL2 | PC5 | PA5 |
| UART (gateway) | LPUART1 (PA2/PA3) | USART1 (PB6/PB7) |
| LED | PB9 | PB5 |
| BOOT | PA0 | PB13 |

### 2. `apps/lora_bridge/` — Co-processor Firmware App

| File | Purpose |
|------|---------|
| `prj.conf` | ZBUS, shell, LORA_RADIO=y (real SX126x), LORA_RADIO_SHELL=y, UART_LORA_SENDER=y, sensor event + log. No fake sensors, LVGL, HTTP, MQTT |
| `CMakeLists.txt` | Standard pattern: `find_package(Zephyr)`, `project()`, `target_sources()` |
| `src/main.c` | `LOG_MODULE_REGISTER(lora_bridge, LOG_LEVEL_INF)` + `return 0` (ADR-008 Rule 4) |
| `boards/wio_e5_mini.overlay` | Enable SubGHz SPI radio, USART1 @ 9600 baud, RF switch GPIOs active-high, LED PB5 active-low |
| `boards/wio_e5_mini_renode.conf` | `CONFIG_LORA_RADIO_FAKE=y` — use fake LoRa driver when running in Renode |

**Kconfig summary (`prj.conf`):**
```kconfig
CONFIG_ZBUS=y
CONFIG_SHELL=y
CONFIG_SENSOR_EVENT=y
CONFIG_SENSOR_EVENT_LOG=y
CONFIG_LORA_RADIO=y
CONFIG_LORA_RADIO_FAKE=n
CONFIG_LORA_RADIO_SHELL=y
CONFIG_UART_LORA_SENDER=y
CONFIG_UART_LORA_SENDER_DEV_NAME="usart1"
```

### 3. `lib/uart_lora_sender/` — UART TX Bridge (NEW)

Mirror of `lib/uart_lora_bridge/` but for the sending (co-processor) side.

| File | Purpose |
|------|---------|
| `Kconfig` | `CONFIG_UART_LORA_SENDER` (depends on ZBUS, SENSOR_EVENT, SERIAL), `CONFIG_UART_LORA_SENDER_DEV_NAME` (default `usart1`) |
| `CMakeLists.txt` | Single source file |
| `include/uart_lora_sender/uart_lora_sender.h` | Empty header (no public API) |
| `src/uart_lora_sender.c` | Self-registers via `SYS_INIT(APPLICATION, 92)`. Subscribes to `sensor_event_chan`. On event: packs `env_sensor_data` into `uart_lora_wire` struct (20 bytes LE), computes CRC8-CCITT, sends 24-byte framed packet over UART via `uart_poll_out()`. Magic bytes: 0x5A 0xA5. |

**Wire format** (same as `lib/uart_lora_bridge/`):
```
[0x5A][0xA5][len=20][20-byte uart_lora_wire][crc8]
```

The `uart_lora_wire` struct and CRC8 table are duplicated from `lib/uart_lora_bridge/` to avoid cross-library dependencies. Both libraries define the same format independently.

### 4. `simulation/renode/wio_e5_mini/` — Renode Model (NEW)

| File | Purpose |
|------|---------|
| `wio_e5_mini.repl` | Platform description: Cortex-M4 @ 48 MHz, 256KB flash @ 0x08000000, 64KB SRAM @ 0x20000000, USART1 @ 0x40013800 with analyzer backend, Sub-GHz SPI @ 0x58010000 (stub registers), GPIO ports A/B/C with RF switch and LED pins, NVIC with STM32WL interrupt layout |
| `wio_e5_mini.resc` | Boot script: `sysbus LoadELF`, enable UART analyzer on USART1, connect UART analyzer to file or terminal |
| `wio_e5_mini_loopback.resc` | Placeholder for RENODE-PHASE2: dual-machine simulation with gateway MCXN947 connected via UART |

**Renode scope this iteration:**

| Included | Deferred |
|----------|----------|
| Cortex-M4 + memory map | Full SX126x peripheral model (Python peripheral) |
| USART1 with analyzer backend | Sub-GHz SPI Python peripheral |
| GPIO stubs for RF switch + LED | Two-machine UART loopback test |
| Boot → reach main() → UART output | Robot Framework tests (`tests/` + `.robot` files) |
| Fake LoRa driver for Renode builds | Real radio register-level simulation |

## Design Constraints

| Constraint | Source |
|------------|--------|
| `main.c` = `LOG_MODULE_REGISTER + return 0` only | ADR-008 Rule 4 |
| All logic in libraries, self-wired via `SYS_INIT` | ADR-008 Rule 1 |
| App composes features via Kconfig only | ADR-008 Rule 2 |
| `env_sensor_data` is flat struct, no heap | ADR-003 |
| zbus channel ownership: one `ZBUS_CHAN_DEFINE` per channel | ADR-002 |
| UART protocol uses simple framed format (not zbus proxy agent) | ADR-015 (deferred) |
| Wire format: 0x5A 0xA5 magic, 20-byte payload, CRC8 | `lib/uart_lora_bridge/` |
| Sensor UID ranges: 0x0021–0x00FF for remote sensors | Sensor UID allocation table |

## Acceptance Criteria

1. ✅ `west build -b wio_e5_mini apps/lora_bridge` succeeds (hardware build)
2. ✅ `west build -b wio_e5_mini apps/lora_bridge -- -DCONFIG_LORA_RADIO_FAKE=y` succeeds (Renode build)
3. ✅ LoRa radio initializes with SX126x driver (EU868, SF10, BW125) — verified via shell `lora status`
4. ✅ `lib/uart_lora_sender/` subscribes to `sensor_event_chan` and outputs framed 24-byte packets on USART1
5. ✅ Renode model boots → reaches `main()` → UART console output visible via analyzer
6. ✅ CRC8 computation matches `lib/uart_lora_bridge/` (unit test or verified by inspection)
7. ❌ Two-machine loopback test (deferred to RENODE-PHASE2)
8. ❌ Real hardware flash/debug (requires physical Wio-E5 Mini + ST-LINK)

## File Tree

```
boards/wio_e5_mini/                    (NEW)
├── Kconfig.wio_e5_mini
├── board.cmake
├── wio_e5_mini.dts
├── wio_e5_mini_defconfig
└── wio_e5_mini-pinctrl.dtsi

apps/lora_bridge/                      (NEW)
├── CMakeLists.txt
├── prj.conf
├── src/
│   └── main.c
└── boards/
    ├── wio_e5_mini.overlay
    └── wio_e5_mini_renode.conf

lib/uart_lora_sender/                  (NEW)
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── uart_lora_sender/
│       └── uart_lora_sender.h
└── src/
    └── uart_lora_sender.c

simulation/renode/wio_e5_mini/         (NEW)
├── wio_e5_mini.repl
├── wio_e5_mini.resc
└── wio_e5_mini_loopback.resc
```
