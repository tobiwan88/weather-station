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

This avoids the overhead of two west build targets, an inter-device protocol,
and two separate binaries to coordinate in tests — all before the core sensor
and MQTT logic is proven.

The combination is **safe to reverse later** because gateway and display
communicate **only via zbus**. Splitting them into separate MCUs is a
configuration change — removing Kconfig symbols from each `prj.conf` and
adding a UART bridge — with no source changes to any library. The
zbus-only coupling rule is enforced by convention and CI include-path checks:
`lib/display_manager/` must have zero `#include` of anything from
`lib/connectivity/` and vice versa.

For the architecture diagram within the single image, button responsibilities,
display routing via `sensor_registry`, and the future split path table, see
[`docs/architecture/system-overview.md`](../architecture/system-overview.md).

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

---

## See also

- Current implementation: `lib/lvgl_display/`, `apps/gateway/`
- Architecture: [`docs/architecture/system-overview.md`](../architecture/system-overview.md) (library roles, extension points)
- Related ADRs: [ADR-002](ADR-002-zbus-as-system-bus.md) (zbus decoupling), [ADR-008](ADR-008-kconfig-app-composition.md) (Kconfig split path), [ADR-009](ADR-009-native-sim-first.md) (native_sim first)
