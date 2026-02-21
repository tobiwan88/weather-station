# Backlog

## [DISPLAY] Add LVGL + SDL graphical clock/sensor display for native_sim

Replace the console clock log with a real pixel window using:
- `CONFIG_LVGL=y` + SDL display driver (`CONFIG_SDL_DISPLAY=y`)
- LVGL widget: digital clock (HH:MM) + last sensor readings per UID
- Triggered by the same `clock_display` log events and `sensor_event_chan`

Prerequisite: NTP sync + console clock (feat/ntp-clock-display) must be merged first.
