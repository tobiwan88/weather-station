# ADR-007 — Gateway and Display as One Device (Initial Phase)

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |
| **Review trigger** | When hardware is purchased / when display latency is a concern |

---

## Context

The original vision sketched two indoor devices: a **gateway** (Wi-Fi + MQTT +
LoRa RX) and a **display unit** (LVGL screen + buttons). In a mature
implementation these might be separate PCBs connected via I2C, SPI, UART, or
even a secondary LoRa/BLE link — for example, a low-power e-ink display on a
different MCU.

However, for an initial `native_sim` implementation, splitting into two
separate firmware images adds substantial complexity with no benefit:

- Two west build targets to maintain
- An inter-device protocol to design (what format? what transport?)
- Two separate native_sim binaries to run and coordinate in tests
- A risk of the split becoming the focus rather than the sensor/MQTT logic

The sketched architecture diagram labels the central node **"gateway + Display"**,
indicating the founder's own mental model already combines them.

```
From sketch (Image 1):

    [WEB]
      │ HTTP (Config, Dashboard)
      │
  ┌───┴──────────────┐
  │ Gateway          │
  │ with Display     │◄──── LoRa ────[Sensor Node]
  └───┬──────────────┘
      │ MQTT
      ▼
  [MQTT SERVER]
```

---

## Decision

For v1, the **`apps/gateway/`** firmware image contains both the gateway
logic (Wi-Fi, MQTT, HTTP, LoRa RX) and the display logic (LVGL, button
handler). They run in the same Zephyr image on the same MCU.

### Architecture within the single image

Even though gateway and display are in one image, they are **architecturally
separate modules** communicating only via zbus. The display manager never
calls MQTT functions. The MQTT publisher never calls LVGL functions.

```
                        apps/gateway (single firmware image)
┌───────────────────────────────────────────────────────────────┐
│                                                               │
│  SENSOR PRODUCERS              SHARED BUS     CONSUMERS       │
│  ─────────────────             ──────────     ─────────       │
│                                                               │
│  [fake_temp_indoor] ──┐                                       │
│  [fake_hum_indoor]  ──┤                  ┌──► [display_mgr]  │
│  [fake_temp_outdoor]──┤─► sensor_event ──┤    (LVGL thread)  │
│  [fake_hum_outdoor] ──┤       _chan      ├──► [mqtt_manager] │
│  [lora_rx thread]   ──┘                  └──► [flash_storage]│
│                                               (future)        │
│                                                               │
│  [periodic timer] ──┐                                         │
│  [button B3]      ──┤─► sensor_trigger_chan ──► all sensors   │
│  [mqtt command]   ──┘                                         │
│                                                               │
│  Wi-Fi ──► HTTP server  (config page, port 8080)              │
│        ──► MQTT client  (→ Mosquitto)                         │
│                                                               │
│  Display hardware ──► LVGL ──► display_manager subscriber     │
│  Button B1-B4     ──► button handler ──► sensor_trigger_chan  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

### Why this split is safe to reverse later

Because gateway and display communicate **only via zbus**, splitting them into
separate devices in a future version requires:

1. Create `apps/display-unit/` as a new firmware image.
2. The display unit subscribes to `sensor_event_chan` via whatever transport
   connects the two MCUs (UART, SPI, second BLE/LoRa link).
3. The gateway image removes `CONFIG_DISPLAY_MANAGER=y` from `prj.conf`.
4. The display unit image removes `CONFIG_MQTT_LIB=y`, `CONFIG_WIFI=y`, etc.

**No changes** to `lib/display_manager/`, `lib/connectivity/`, or any sensor
driver code. The zbus-first architecture makes the split a configuration
change, not a refactor.

### Display UI specification (from sketch Image 3)

```
┌────────────────────────────────────────────────────────┐
│                                                        │
│  ┌───────────────────────┬──────────────────────────┐  │
│  │  INDOOR               │  WEATHER FORECAST        │  │
│  │                       │                          │  │
│  │  Living Room  33°C    │  (future: OpenWeather    │  │
│  │  Air          50%     │   or local computation)  │  │
│  │                       │                          │  │
│  │  OUTDOOR              │                          │  │
│  │  4°C   30%   Air :)   │                          │  │
│  └───────────────────────┴──────────────────────────┘  │
│                                                        │
│  [B1: ◄ prev]  [B2: next ►]  [B3: refresh]  [B4: ☀]  │
└────────────────────────────────────────────────────────┘
```

Button responsibilities:
| Button | zbus action |
|--------|------------|
| B1 | Previous screen (display-internal) |
| B2 | Next screen (display-internal) |
| B3 | Publish `sensor_trigger_event` with `TRIGGER_SOURCE_BUTTON` |
| B4 | Settings / backlight (display-internal) |

On `native_sim`, buttons are simulated by the shell:
```
uart:~$ fake_sensors trigger       ← equivalent to B3 press
```

### Display routing via `sensor_registry`

The display manager never hardcodes "uid 0x0001 is indoor temperature".
Instead it looks up `sensor_registry_lookup(uid)->location` and routes
values to the corresponding UI tile:

```c
void on_sensor_event(const struct zbus_channel *chan)
{
    const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
    const struct sensor_meta *meta = sensor_registry_lookup(evt->sensor_uid);
    if (!meta) return;

    if (strcmp(meta->location, "living_room") == 0) {
        display_update_indoor_tile(evt->type,
                                   q31_to_display_value(evt));
    } else if (strcmp(meta->location, "outdoor") == 0) {
        display_update_outdoor_tile(evt->type,
                                    q31_to_display_value(evt));
    }
}
```

Adding a new room (e.g. "garage") requires adding a new tile to the LVGL
layout — no change to the routing logic.

---

## Consequences

**Easier:**
- One `west build` target — simpler CI, simpler developer experience.
- No inter-device protocol to design or debug.
- LVGL and MQTT code can share the same zbus channel without serialisation.
- native_sim binary captures the full system behaviour in one process.

**Harder:**
- A large or power-hungry display on the same MCU as Wi-Fi + LoRa may require
  careful stack/heap sizing and power management. This is deferred to hardware
  selection.
- If the MCU doesn't have enough RAM for LVGL + MQTT + LoRa simultaneously,
  the split becomes mandatory. The architecture already supports this.

**Constrained:**
- The display must use `DEVICE_DT_GET(DT_CHOSEN(zephyr_display))` — never
  a specific driver node. This ensures the `apps/display-unit/` future split
  works by simply changing the board overlay.
- `lib/display_manager/` must have zero `#include` of anything from
  `lib/connectivity/` and vice versa. Validate with CI include-path checks.

---

## Future split path

When hardware is selected and a split becomes desirable:

```
Phase 1 (current):           Phase 2 (future):
apps/gateway/                apps/gateway/        apps/display-unit/
  prj.conf:                    prj.conf:            prj.conf:
    WIFI=y                       WIFI=y               DISPLAY=y
    MQTT=y                       MQTT=y               LVGL=y
    LORA=y                       LORA=y               DISPLAY_MANAGER=y
    DISPLAY=y        ──►         DISPLAY=n            WIFI=n
    LVGL=y                       LVGL=n               MQTT=n
    DISPLAY_MANAGER=y            DISPLAY_MANAGER=n    LORA=n
                                 UART_BRIDGE=y        UART_BRIDGE=y
                                     │                    │
                                     └──── UART ──────────┘
                                     (serialised zbus events)
```

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Separate display firmware from day one | Doubles development surface area before core logic is proven; requires inter-device protocol before hardware is chosen |
| Separate process on native_sim (socket IPC) | Complex test setup; hides bugs in the protocol rather than the application logic |
| Display driven by MQTT subscribe (display as MQTT client) | Adds broker dependency for local display; latency; unnecessary for co-located components |
