# REQ-DISPLAY — LVGL Display Requirements

## Status
review

## Version
0.1

## Context
The display subsystem renders sensor data on an LVGL-based UI (320x240 SDL window for native_sim). It includes an analog clock, sensor cards, and subscribes to sensor events via zbus. The display runs as part of the gateway firmware (combined device per ADR-007).

## Functional Requirements

### REQ-DISPLAY-001 — LVGL Initialization
The system shall initialize LVGL with a 320x240 SDL window on native_sim, using `DEVICE_DT_GET(DT_CHOSEN(zephyr_display))` for display device selection — never a specific driver node.

### REQ-DISPLAY-002 — Analog Clock
The system shall display an analog clock showing wall-clock time from `sntp_sync`, updated every 60 seconds.

### REQ-DISPLAY-003 — Sensor Cards
The system shall render sensor data as cards on the LVGL display, with one card per sensor location showing the latest measurement values.

### REQ-DISPLAY-004 — Sensor Event Subscription
The system shall subscribe to `sensor_event_chan` and update the display when new sensor events arrive. The subscription handler must not block or sleep (runs in publisher's context).

### REQ-DISPLAY-005 — Sensor Registry Routing
The system shall route sensor events to display tiles using `sensor_registry` lookup by sensor_uid — no hardcoded UID-to-tile mapping.

### REQ-DISPLAY-006 — Screen Navigation
The system shall support screen navigation via button input:
- B1: Previous screen (display-internal)
- B2: Next screen (display-internal)
- B4: Settings / backlight (display-internal)
- B3: Publish `sensor_trigger_event` with `TRIGGER_SOURCE_BUTTON`

### REQ-DISPLAY-007 — Library Isolation
The system shall maintain zero `#include` dependencies between `lib/lvgl_display/` and `lib/connectivity/` libraries. This shall be enforced by CI include-path checks.

### REQ-DISPLAY-008 — ADR-008 Compliance
The system shall not be called directly from `main.c`. The display shall self-initialize via `SYS_INIT` and run its own thread for the LVGL timer loop.

### REQ-DISPLAY-009 — Display Blanking
The system shall call `display_blanking_off()` after UI creation to ensure the display is visible.

## Dependencies
- REQ-TIME-001: SNTP time synchronization for clock display
- REQ-SENSORS-005: Sensor UID identity for card routing
- REQ-LOCATION-001: Location registry for card labels

## Constraints
- LVGL render loop must run on main thread (SDL requirement on Linux)
- No direct calls from display to connectivity libraries (ADR-007)
- Display routing uses sensor_registry — no hardcoded mappings

## Acceptance Criteria
- [ ] [native_sim] Integration: LVGL window opens on native_sim build
- [ ] [native_sim] Integration: Analog clock displays correct time from sntp_sync
- [ ] [native_sim] Integration: Sensor cards update on new sensor events
- [ ] [native_sim] Integration: Screen navigation via B1/B2 buttons works
- [ ] [native_sim] Integration: sensor_registry lookup routes events to correct cards
- [ ] [native_sim] ztest: No #include of connectivity headers in lvgl_display source
- [ ] [renode] Integration: Display initialization completes without SDL
- [ ] [manual] Board: LVGL renders on physical display (FRDM-MCXN947 + shield)

## Related ADRs
- ADR-007: Gateway and Display as One Device (initial phase)
- ADR-008: Kconfig-Only App Composition
- ADR-002: zbus as System-Wide Communication Fabric

## Related Zephyr subsystems
- CONFIG_LVGL
- CONFIG_DISPLAY
- CONFIG_VIDEO
- CONFIG_SDL
- CONFIG_LVGL_SDL
