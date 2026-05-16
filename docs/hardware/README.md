# Hardware

This document describes the physical hardware for the Weather Station project — the gateway and outdoor sensor nodes — including specifications, pin mappings, and wiring references.

## System Topology

```
                         ┌──────────────────────────────────────┐
                         │  Gateway (FRDM-MCXN947)               │
                         │                                      │
                         │  ┌──────────┐  ┌──────────────────┐  │
                         │  │ ILI9341  │  │ BME688 (indoor)  │  │
┌─────────┐              │  │ 2.4" TFT │  │ T/H/P/VOC        │  │
│  Web    │◄── HTTP ─────┤  │ SPI      │  │ I2C 0x76         │  │
│ Browser │              │  └──────────┘  └──────────────────┘  │
└─────────┘              │                                      │
                         │  ┌──────────────────────────────────┐ │
┌─────────┐              │  │ Wio-E5 Mini (LoRa co-processor)  │ │
│  MQTT   │◄── MQTT ─────┤  │ STM32WLE5JC                     │ │
│ Broker  │              │  │ UART → Flexcomm5                │ │
└─────────┘              │  │ LoRa P2P                        │ │
                         │  └────────────┬─────────────────────┘ │
                         └───────────────┼───────────────────────┘
                                         │   ~ LoRa P2P ~
                                         │   EU868 / SF10-BW125
                       ┌─────────────────┼───────────────────────┐
                       │ Outdoor Sensor Node 1 (Wio-E5 Mini)     │
                       │                                         │
                       │  ┌──────────────────┐  ┌─────────────┐  │
                       │  │ BME688 (outdoor) │  │ SEN0460     │  │
                       │  │ T/H/P/VOC        │  │ PM1.0/2.5/10│  │
                       │  │ I2C 0x76         │  │ I2C 0x19    │  │
                       │  └──────────────────┘  └─────────────┘  │
                       └─────────────────────────────────────────┘
```

The topology follows [ADR-015](../adr/ADR-015-lora-protocol.md): the gateway uses a dedicated STM32WLE5JC LoRa co-processor connected via UART. The outdoor sensor node is a self-contained Zephyr device that reads sensors and transmits compact 5-byte readings over LoRa P2P. The gateway and display are combined in a single firmware image per [ADR-007](../adr/ADR-007-gateway-display-combined.md).

---

## Gateway — FRDM-MCXN947

**NXP FRDM-MCXN947 development board** — the main gateway MCU.

| Parameter | Value |
|---|---|
| **SoC** | NXP MCXN947 (dual ARM Cortex-M33) |
| **CPU frequency** | 150 MHz per core |
| **On-chip flash** | 2 MB (dual-bank, MCUboot-compatible) |
| **On-chip SRAM** | 512 KB |
| **External flash** | 8 MB W25Q64JV (QSPI NOR) |
| **External RAM** | None (on-chip only) |
| **Hardware crypto** | EdgeLock Secure Subsystem, TRNG, AES |
| **Coproc accelerators** | NPU, PowerQuad DSP |
| **Cost** | ~$25 USD |

### Connectivity

| Interface | Detail |
|---|---|
| **Ethernet** | On-board RJ45 with PHY, ENET-QOS MAC, IEEE 1588 PTP |
| **USB** | Dual Type-C (HS USB + FS OTG) |
| **Debug** | On-board MCU-Link (CMSIS-DAP v2) via USB Micro-B; 10-pin SWD header |
| **Serial** | Virtual COM port via MCU-Link USB |
| **microSD** | On-board slot (USDHC controller) |
| **CAN-FD** | On-board transceiver |

### Expansion Headers

| Header | Interface | Used For |
|---|---|---|
| Arduino Uno R3 | SPI, I2C, UART, GPIO, ADC | ILI9341 display, BME688 indoor sensor, Wio-E5 reset |
| FlexIO LCD | 16-bit 8080 parallel | *(not used — SPI display via Arduino instead)* |
| mikroBUS | SPI, I2C, UART | *(available for future expansion)* |
| FRDM expansion | Extra GPIO | *(available)* |

### Flexcomm Assignments (Gateway Build)

| Flexcomm | Mode | Use |
|---|---|---|
| FC1 | LPSPI1 | ILI9341 display (Arduino SPI: D10–D13) |
| FC2 | LPI2C2 | BME688 (Arduino I2C: D14–D15) |
| FC5 | LPUART5 | Wio-E5 co-processor UART (PT1_4/PT1_5) |
| FC4 | LPUART4 | Console/shell UART |

---

## Gateway — ILI9341 2.4" TFT Display

**2.4" SPI TFT LCD module** with ILI9341 display driver.

| Parameter | Value |
|---|---|
| **Resolution** | 320 × 240 pixels (QVGA) |
| **Color depth** | 65K colors (RGB 5-6-5) |
| **Display driver** | ILITEK ILI9341 |
| **Interface** | 4-wire SPI |
| **Touch** | Optional resistive (XPT2046, separate SPI) |
| **Backlight** | 4× white LED, PWM-dimmable |
| **Module supply** | 3.3V–5V (onboard LDO) |
| **Cost** | ~$8 USD |

### Pin Mapping — ILI9341 to MCXN947 Arduino Header

| ILI9341 Pin | Signal | Arduino Pin | MCXN947 Port.Pin | Notes |
|---|---|---|---|---|
| 1 | VCC | 3.3V | — | Power |
| 2 | GND | GND | — | Ground |
| 3 | CS | **D8** | PT0_28 | SPI chip select (GPIO) |
| 4 | RESET | **D4** | PT0_30 | Hardware reset |
| 5 | DC (RS) | **D7** | PT0_31 | Data/command select |
| 6 | SDI (MOSI) | **D11** | PT0_24 | SPI1 MOSI (FC1_P0) |
| 7 | SCK | **D13** | PT0_25 | SPI1 SCK (FC1_P1) |
| 8 | LED | **D5** | PT1_21 | PWM backlight (SCT0_OUT9) |
| 9 | SDO (MISO) | **D12** | PT0_26 | SPI1 MISO (FC1_P2) — read-only, optional |

### Touch Panel (Optional, XPT2046)

| Touch Pin | Arduino Pin | MCXN947 Pin | Notes |
|---|---|---|---|
| T_CLK | D13 | PT0_25 | Shares SPI SCK |
| T_CS | D9 | PT0_10 | Touch chip select (⚠ shares with red LED) |
| T_DIN | D11 | PT0_24 | Shares SPI MOSI |
| T_DO | D12 | PT0_26 | Shares SPI MISO |
| T_IRQ | D3 | PT1_23 | Touch interrupt |

### ILI9341 Initialization Notes

Minimal SPI init sequence: hardware reset (≥1 ms low → high, wait ≥120 ms), software reset (0x01), power control (0xC0–0xC8), memory access control (0x36), pixel format 16-bit (0x3A=0x55), sleep out (0x11 + 120 ms), display ON (0x29). Zephyr's `zephyr,ili9341` display driver handles this, but LVGL will manage the framebuffer in software since the ILI9341's internal GRAM is only 172,800 bytes — exact match for 320×240×18-bit.

---

## Gateway — BME688 Indoor Environmental Sensor

**Adafruit BME688 QT** (Product ID 5046) — temperature, humidity, pressure, and gas/VOC sensor on a STEMMA QT breakout.

| Parameter | Value |
|---|---|
| **Sensor chip** | Bosch BME688 |
| **Measures** | Temperature, relative humidity, barometric pressure, gas resistance (VOC/IAQ) |
| **Temperature range** | −40 to +85 °C, ±1.0 °C accuracy |
| **Humidity range** | 0–100 %RH, ±3 %RH accuracy |
| **Pressure range** | 300–1100 hPa, ±1 hPa accuracy |
| **Gas sensor** | Heated MOX, detects VOCs, ethanol, CO, H₂, H₂S |
| **Interface** | I2C (STEMMA QT / Qwiic, JST SH 4-pin) + SPI breakout pads |
| **I2C addresses** | 0x76 (SDO low, default), 0x77 (SDO high) |
| **Supply voltage** | 3–5V (onboard 3.3V LDO + level shifting) |
| **Board dimensions** | 25.5 × 17.6 mm |
| **Gas scan current** | 3.9 mA (standard), 90 µA (ULP mode) |
| **Cost** | ~$20 USD |

### Pin Mapping — BME688 to MCXN947 Arduino I2C

| BME688 QT | Arduino Pin | MCXN947 Pin | Notes |
|---|---|---|---|
| VCC (red) | 3.3V | — | Power |
| GND (black) | GND | — | Ground |
| SCL (yellow) | **D15 (SCL)** | PT4_1 | I2C2 SCL (FC2_P1) |
| SDA (blue) | **D14 (SDA)** | PT4_0 | I2C2 SDA (FC2_P0) |

Default I2C address 0x76 (SDO pulled to GND via onboard 10K resistor). Leave the SDO breakout pad unconnected.

### Gas Sensing

The BME688's MOX sensor produces a single gas resistance value per scan. Raw values require trend comparison or BSEC library processing to derive IAQ index, bVOC ppm, and CO₂ equivalent ppm. The BME AI-Studio desktop tool can train gas classification models (e.g. "coffee", "spoiled food") and export configuration strings for on-device classification. BSEC integration is deferred — the initial implementation reads raw T/P/H/gas resistance via [BME68x-Sensor-API](https://github.com/BoschSensortec/BME68x-Sensor-API).

Zephyr upstream provides `CONFIG_BME680` which covers BME688 in compatibility mode (T/P/H/gas resistance).

---

## Gateway — Wio-E5 Mini LoRa Co-Processor

**Seeed Studio Wio-E5 Mini** (STM32WLE5JC) — dedicated LoRa radio co-processor, running custom Zephyr firmware with `lib/lora_radio/`. Role defined in [ADR-015](../adr/ADR-015-lora-protocol.md).

| Parameter | Value |
|---|---|
| **MCU** | STM32WLE5JC (ARM Cortex-M4 @ 48 MHz) |
| **Flash / SRAM** | 256 KB / 64 KB |
| **LoRa radio** | Integrated Semtech SX126x (licensed IP block) |
| **Frequency bands** | EU868, US915, AS923, AU915, KR920, IN865, RU864 |
| **TX power** | +22 dBm max (high-power PA) |
| **RX sensitivity** | −148 dBm (SF12, 10.4 kHz BW, chip-level) |
| **Link budget** | 158 dB |
| **LoRaWAN** | Class A / B / C, MAC 1.0.2 (factory AT firmware; superseded by custom firmware) |
| **P2P modes** | LoRa, (G)FSK, (G)MSK, BPSK |
| **Active TX current** | 87 mA @ 20 dBm; 111 mA @ 22 dBm |
| **Active RX current** | 4.82 mA |
| **Sleep (WOR)** | 2.1 µA |
| **Hardware crypto** | AES 256-bit, TRNG, PKA |
| **Antenna** | IPEX/u.FL connector; SMA-K with included antenna |
| **Module dimensions** | 50 × 23 mm (dev board) |
| **Cost** | ~$15 USD |

### Pin Mapping — Wio-E5 Mini to MCXN947

| Wio-E5 Pin | Signal | MCXN947 Pin | Flexcomm | Notes |
|---|---|---|---|---|
| 3.3V | Power | 3.3V | — | |
| GND | Ground | GND | — | |
| TX (PB6) | UART TX → | **PT1_5** | FC5_P1 (RX) | LPUART5, 9600 baud |
| RX (PB7) | UART RX ← | **PT1_4** | FC5_P0 (TX) | LPUART5, 9600 baud |
| NRST | Reset | **D2 (PT0_29)** | — | GPIO output, active low |

Flexcomm5 (LPUART5) is chosen because:
- Its pins (PT1_4–PT1_7) are not routed to the Arduino header and are otherwise unused on the FRDM-MCXN947
- Flexcomm0 is unavailable (SWD debug pins PT0_0/PT0_1)
- Flexcomm9 pins conflict with the microSD slot (PT2_2–PT2_5)

### Wio-E5 Firmware

The factory AT command firmware (LoRaWAN Class A/B/C, 9600 baud) is **replaced** with custom Zephyr firmware built from the same `apps/` tree. The custom firmware runs `lib/lora_radio/` with local zbus channels. Communication between the MCXN947 gateway and the Wio-E5 co-processor uses a UART zbus proxy agent (deferred per ADR-015 — initial implementation may use a simpler framed protocol over UART).

Programming: SWD via ST-LINK/V2. The factory firmware has RDP Level 1 read protection — a full flash mass erase is required before flashing custom firmware. The factory AT firmware cannot be restored after erasing.

**Wio-E5 Mini GPIO re-mappings vs NUCLEO-WL55JC** (important for custom firmware):

| Function | NUCLEO-WL55JC | Wio-E5 Mini |
|---|---|---|
| RF_CTRL1 | PC4 | PA4 |
| RF_CTRL2 | PC5 | PA5 |
| USART (AT) | USART2 (PA2/PA3) | USART1 (PB6/PB7) |
| LED | PB9 | PB5 |
| BOOT | PA0 | PB13 |

RF switch control for high-power TX: PA4=0, PA5=1. Only RFO_HP supported — no low-power TX path.

---

## Outdoor Sensor Node 1 — Wio-E5 Mini

The outdoor sensor node is a self-contained Zephyr device built around the Wio-E5 Mini module. It reads local sensors, encodes readings in the compact 5-byte wire format defined in [ADR-015](../adr/ADR-015-lora-protocol.md), and transmits them over LoRa P2P to the gateway.

**MCU and radio:** same Wio-E5 Mini module as the gateway co-processor (see specs above).

### Firmware

The sensor node runs custom Zephyr firmware as a separate Zephyr application. The build target is based on the `nucleo_wl55jc` board definition, adapted via an overlay to match the Wio-E5 Mini pinout (different UART, RF control GPIOs, and LED mappings — see table above).

Key firmware responsibilities:
- Read BME688 (T/H/P/gas resistance) and SEN0460 (PM1.0/PM2.5/PM10) via I2C
- Encode readings in compact 5-byte wire format (1B sensor_type + 4B q31_value)
- Transmit via `lib/lora_radio/` using the protocol defined in ADR-015
- Sleep in WOR mode (2.1 µA) between measurement cycles
- Respond to trigger commands and RPC from gateway
- Support button-press pairing (Ed25519 + AES-128-GCM session key exchange)

### Power

| Mode | Current | Notes |
|---|---|---|
| Sleep (WOR) | 2.1 µA | LoRa wake-on-radio |
| Stop2 + RTC | 1.07 µA | No LoRa RX |
| Standby + RTC | 360 nA | |
| Active RX | 4.82 mA | LoRa receive |
| Active TX (20 dBm) | 87 mA | LoRa transmit |
| Active TX (22 dBm) | 111 mA | Max power |
| BME688 (1 Hz T+H+P) | 3.7 µA | |
| BME688 (standard gas) | 3.9 mA | During active scan |
| SEN0460 (active) | ~100 mA | During measurement |
| SEN0460 (standby) | ≤2 mA | |

**Battery life estimate** (2000 mAh LiPo, 60s interval, T+H+P + PM2.5):

A measurement cycle: wake (1 ms) → BME688 read (~10 ms @ 3.7 µA) → SEN0460 warm-up + read (~2 s @ 100 mA) → LoRa TX (~200 ms @ 87 mA) → sleep (~57.8 s @ 2.1 µA).
Approximate average current: ~3.4 mA → **~24 days** on a single 2000 mAh battery.
With solar charging (e.g., 2W panel), indefinite operation is achievable.

**Programming:** SWD via ST-LINK/V2 (external debug probe pads on the Wio-E5 Mini).

**Certification:** FCC, CE, IC, TELEC certified — ready for outdoor deployment.

---

## Outdoor Sensor Node 1 — BME688 Outdoor Environmental Sensor

Same Adafruit BME688 QT board as the gateway indoor sensor. Provides outdoor temperature, humidity, pressure, and gas resistance. Connected via I2C on the Wio-E5 Mini's I2C2 bus at address 0x76.

### Pin Mapping — BME688 QT to Wio-E5 Mini

| BME688 QT | Wio-E5 Mini Pin | GPIO | Notes |
|---|---|---|---|
| VCC (red) | 3.3V | — | Power |
| GND (black) | GND | — | Ground |
| SCL (yellow) | SCL | PA12 | I2C2 SCL |
| SDA (blue) | SDA | PA11 | I2C2 SDA |

### Outdoor Enclosure Notes

The BME688 sensor die has an exposed metal lid — it **must** be housed in a weatherproof enclosure with ventilation for accurate environmental readings. Recommended: a Stevenson screen or louvered radiation shield to prevent direct solar heating from skewing temperature readings. A small fan or passive airflow design improves gas sensor response.

---

## Outdoor Sensor Node 1 — SEN0460 PM2.5 Particulate Sensor

**DFRobot Gravity SEN0460** — PM2.5/PM1.0/PM10 laser particle sensor.

| Parameter | Value |
|---|---|
| **Sensor type** | Laser scattering |
| **Measures** | PM1.0, PM2.5, PM10 mass concentration (µg/m³) + particle counts per 0.1L |
| **Particle size range** | 0.3–10 µm |
| **Counting efficiency** | 50% @ 0.3 µm, 98% @ ≥0.5 µm |
| **PM2.5 range** | 0–500 µg/m³ (effective), ≥1000 µg/m³ (max) |
| **PM2.5 resolution** | 1 µg/m³ |
| **PM2.5 accuracy** | ±10 µg/m³ (0–100), ±10% (100–500) |
| **Response time** | <1s single reading, ≤10s comprehensive |
| **Operating temp** | −10 to +60 °C |
| **Operating humidity** | 0–95% RH (non-condensing) |
| **Interface** | I2C (Gravity 4-pin PH2.0) |
| **I2C address** | 0x19 |
| **Supply voltage** | 3.3V–5.0V |
| **Active current** | ~100 mA |
| **Standby current** | ≤2 mA |
| **MTBF** | ≥5 years |
| **Dimensions** | 72 × 40 × 14 mm |
| **Cost** | ~$40 USD |

### Pin Mapping — SEN0460 to Wio-E5 Mini

| SEN0460 (Gravity) | Wio-E5 Mini Pin | GPIO | Notes |
|---|---|---|---|
| VCC (red) | 3.3V | — | Power (3.3V sufficient) |
| GND (black) | GND | — | Ground |
| SCL (yellow) | SCL | PA12 | I2C2 SCL (shared with BME688) |
| SDA (blue) | SDA | PA11 | I2C2 SDA (shared with BME688) |

### I2C Bus Details — Outdoor Sensor Node 1

Both BME688 (0x76) and SEN0460 (0x19) share the same I2C2 bus (PA11=SDA, PA12=SCL). No address conflict. The DFRobot library requires calling `begin()` after power-on to initialize the sensor — the Zephyr driver must handle this initialization sequence.

### Outdoor Enclosure Notes

The SEN0460 requires exposure to ambient air via an intake vent. The laser scattering chamber must be protected from rain and insects but remain open to airflow. Periodic cleaning of the optical chamber may be needed in dusty environments. The sensor has an internal fan — ensure the enclosure provides adequate airflow.

---

## Pin Conflict Resolution

Several MCXN947 Arduino header pins are shared with on-board peripherals. The gateway board overlay (`apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.overlay`) must disable conflicting nodes:

| Arduino Pin | MCXN947 Pin | Conflicts With | Resolution |
|---|---|---|---|
| D6 | PT1_2 | Blue LED (`&blue_led` / `led_2`) | `status = "disabled"` if used |
| D9 | PT0_10 | Red LED (`&red_led` / `led_3`) | `status = "disabled"` if touch CS used |
| D10 | PT0_27 | Green LED (`&green_led` / `led_0`) | `status = "disabled"` if D10 used as GPIO |
| A5 | PT0_23 | User button SW2 (`&user_button_2`) | `status = "disabled"` if A5 used as GPIO |

The chosen ILI9341 pin mapping avoids D6, D9, and D10 (CS on D8, DC on D7, RST on D4). D10–D13 are used for their hardware SPI function, where the LED conflict with D10 is acceptable since D10 operates as a hardware chip select for LPSPI1.

---

## Zephyr Board Support

### Gateway: `frdm_mcxn947/mcxn947/cpu0`

Fully supported in upstream Zephyr. Key device tree files:

| File | Path |
|---|---|
| Board DTS | `zephyr/boards/nxp/frdm_mcxn947/frdm_mcxn947_mcxn947_cpu0.dts` |
| Pin control | `zephyr/boards/nxp/frdm_mcxn947/frdm_mcxn947-pinctrl.dtsi` |
| Arduino header | Defined in `frdm_mcxn947.dtsi` (compatible `arduino-header-r3`) |
| Pinmux macros | `hal/nxp/dts/nxp/mcx/MCXN947VDF-pinctrl.h` |

Build command:
```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -b frdm_mcxn947/mcxn947/cpu0 apps/gateway
```

### LoRa Co-Processor (Wio-E5 Mini): `nucleo_wl55jc` based

No dedicated board definition exists for the Wio-E5 Mini. The closest upstream board is `nucleo_wl55jc` (same STM32WL55JC SoC). A custom board definition or overlay adapts the pinout for the Wio-E5 Mini's GPIO re-mappings (see table above).

Key device tree files for reference:

| File | Path |
|---|---|
| Board DTS | `zephyr/boards/st/nucleo_wl55jc/nucleo_wl55jc.dts` |
| Arduino header | `zephyr/boards/st/nucleo_wl55jc/arduino_r3_connector.dtsi` |
| SoC DTSI | `zephyr/dts/arm/st/wl/stm32wl55Xc.dtsi` |

### Outdoor Sensor Node 1 (Wio-E5 Mini)

Same board support situation as the co-processor. Uses the same `nucleo_wl55jc` base adapted for Wio-E5 Mini pinout.

---

## Bill of Materials

| Item | Qty | Unit Cost | Total |
|---|---|---|---|
| NXP FRDM-MCXN947 | 1 | $25 | $25 |
| ILI9341 2.4" TFT (SPI) | 1 | $8 | $8 |
| Adafruit BME688 QT | 2 | $20 | $40 |
| Seeed Wio-E5 Mini (EU868) | 2 | $15 | $30 |
| DFRobot SEN0460 PM2.5 | 1 | $40 | $40 |
| **Total** | | | **~$143** |

Additional: antennae (included with Wio-E5), STEMMA QT / Qwiic cables (Adafruit 4214 or equiv), prototyping wires, outdoor enclosure, battery + solar panel (outdoor node).

---

## References

- [NXP FRDM-MCXN947](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-MCXN947) — official product page
- [ILI9341 Datasheet](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf) — ILITEK display controller
- [Adafruit BME688 QT](https://www.adafruit.com/product/5046) — breakout board
- [Bosch BME688 Datasheet](https://www.bosch-sensortec.com/products/environmental-sensors/gas-sensors/bme688/) — sensor specifications
- [BME68x Sensor API](https://github.com/BoschSensortec/BME68x-Sensor-API) — open-source driver (BSD-3)
- [Seeed Wio-E5 Mini](https://wiki.seeedstudio.com/LoRa_E5_mini/) — module wiki
- [STM32WLE5JC Datasheet](https://www.st.com/en/microcontrollers-microprocessors/stm32wle5jc.html) — STM32WL series
- [DFRobot SEN0460](https://wiki.dfrobot.com/SKU_SEN0460_Gravity_PM2.5_Air_Quality_Sensor) — PM2.5 sensor wiki
- [ADR-006](../adr/ADR-006-lora-channel-boundary.md) — LoRa bounded context (superseded)
- [ADR-007](../adr/ADR-007-gateway-display-combined.md) — combined gateway + display
- [ADR-015](../adr/ADR-015-lora-protocol.md) — LoRa protocol architecture (superseding)
