# Hardware Proposal — Real-Hardware Port

> Status: Draft / idea stage. No overlays or drivers written yet.

---

## Requirements

Derived from the current `native_sim` implementation:

| Requirement | Source |
|---|---|
| Wi-Fi (MQTT, HTTP dashboard on port 8080, SNTP) | `apps/gateway/prj.conf` |
| LoRa radio — EU868, SF10/BW125/CR4_5, SX1276 or SX1262 | ADR-006 |
| LVGL display 320×240, 4 buttons (B1–B4) | ADR-007 |
| ≥256 KB RAM (LVGL pool 65 KB + MQTT + networking stacks + RTOS) | `prj.conf` heap/stack config |
| Temperature + humidity sensors on I2C (two separate events per ADR-001) | ADR-003 |

---

## Primary Recommendation

### Gateway — Nordic nRF7002-DK

**Zephyr board target:** `nrf7002dk/nrf5340/cpuapp`

| Feature | Hardware | Notes |
|---|---|---|
| MCU | nRF5340 — dual-core Cortex-M33, 512 KB RAM, 1 MB Flash | Tier 1 Zephyr; Nordic maintains both the board and Wi-Fi driver |
| Wi-Fi | nRF7002 companion chip (on-board, Wi-Fi 6) | Best-supported Wi-Fi in Zephyr tree; same vendor as MCU |
| LoRa | Semtech SX1262MB2xAS Arduino shield | `CONFIG_LORA_SX1262`; fits directly onto DK Arduino headers |
| Display | Adafruit 2.8" ILI9341 SPI TFT (320×240) | `CONFIG_ILI9341`; most-tested SPI display in Zephyr LVGL samples |
| Buttons | 4× tactile switches on GPIO (DK also has 4 on-board buttons) | Map directly to B1–B4 from ADR-007 |

512 KB RAM leaves comfortable headroom: LVGL pool (65 KB) + networking (~80 KB) + MQTT + LoRa driver + RTOS overhead.

---

### Sensor Node — RAK WisBlock RAK4631

**Zephyr board target:** `rak4631_nrf52840`

| Feature | Hardware | Notes |
|---|---|---|
| MCU | nRF52840 — 256 KB RAM, 1 MB Flash | nRF52840 is Tier 1 in Zephyr; RAK4631 board definition is in-tree |
| LoRa | SX1262 built into the RAK4631 module (EU868) | Matches gateway radio config exactly; no extra wiring |
| Sensors | RAK1901 (SHTC3) — or — BME280 I2C breakout | `CONFIG_SENSIRION_SHTCX` / `CONFIG_BME280`; both have in-tree Zephyr drivers; snap onto WisBlock base board |

The WisBlock snap-connector ecosystem requires no soldering to prototype.

---

## Shopping List (primary path)

| Item | Qty | Approx. cost |
|---|---|---|
| Nordic nRF7002-DK | 1 | ~$50 — available from Nordic, Digikey, Mouser |
| Semtech SX1262MB2xAS Arduino shield | 1 | ~$30 |
| Adafruit 2.8" TFT ILI9341 SPI (PID 1770) | 1 | ~$20 |
| RAK WisBlock Starter Kit (RAK19007 base + RAK4631 core) | 1 | ~$30 |
| RAK1901 SHTC3 sensor module | 1 | ~$8 |

---

## Budget / All-in-One Alternative

If fewer separate components are preferred:

**Gateway: Heltec WiFi LoRa 32 V3**
- ESP32-S3 + SX1262 + 0.96" OLED all on one board
- Zephyr board: `heltec_wifi_lora32_v2` (V2 definition works for V3)
- Caveat: built-in OLED is 128×64 — an external ILI9341 is still needed for the 320×240 LVGL UI, or the display target must be redesigned for 128×64

**Sensor node: same RAK4631** as primary, or Heltec Wireless Stick Lite V3

This path cuts cost but adds ESP-IDF HAL complexity for Wi-Fi in Zephyr.

---

## Porting Work Required

1. **Gateway overlay** — `apps/gateway/boards/nrf7002dk_nrf5340_cpuapp.overlay`
   - SX1262 SPI node with CS/BUSY/DIO1/RESET pins
   - ILI9341 SPI node with DC/CS/RESET pins
   - Button GPIO nodes (B1–B4)
   - `chosen { zephyr,display = &ili9341; }`

2. **Sensor node overlay** — `apps/sensor-node/boards/rak4631_nrf52840.overlay`
   - SX1262 is partially defined in the RAK BSP; verify pinout
   - SHTC3 or BME280 I2C node on `&i2c1`

3. **Gateway `prj.conf` board fragment** — `apps/gateway/boards/nrf7002dk_nrf5340_cpuapp.conf`
   - Replace `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y` with `CONFIG_WIFI_NRF700X=y` + `CONFIG_NET_L2_WIFI_MGMT=y`
   - Add `CONFIG_LORA_SX1262=y`, `CONFIG_ILI9341=y`
   - Remove `CONFIG_FAKE_SENSORS=y`, add `CONFIG_BME280=y` (or SHTCX)

4. **Real sensor driver** — a new `lib/bme280_sensor/` (or shtcx) that subscribes to `sensor_trigger_chan` and publishes on `sensor_event_chan`, replacing `fake_sensors` on real hardware. The zbus architecture requires no changes to consumers.

---

## Radio Configuration Reference

Both nodes must share the same LoRa parameters (from ADR-006):

```c
#define LORA_FREQUENCY_HZ    868100000   /* EU868 channel 1 */
#define LORA_BANDWIDTH       BW_125_KHZ
#define LORA_SPREADING       SF_10
#define LORA_CODING_RATE     CR_4_5
#define LORA_PREAMBLE_LEN    8
#define LORA_TX_POWER_DBM    14
#define LORA_PUBLIC_NETWORK  false       /* private sync word */
```

At SF10/BW125 with a 40-byte packet: ~370 ms on-air time → minimum TX interval ~37 s for EU868 1% duty cycle compliance. `CONFIG_SENSOR_POLL_INTERVAL_S` must be ≥ 60.
