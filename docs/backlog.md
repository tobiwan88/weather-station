# Backlog

## [DISPLAY] Add LVGL + SDL graphical clock/sensor display for native_sim

Replace the console clock log with a real pixel window using:
- `CONFIG_LVGL=y` + SDL display driver (`CONFIG_SDL_DISPLAY=y`)
- LVGL widget: digital clock (HH:MM) + last sensor readings per UID
- Triggered by the same `clock_display` log events and `sensor_event_chan`

Prerequisite: NTP sync + console clock (feat/ntp-clock-display) must be merged first.

## Claude memory setuo & setup
default  folder is ~/workspace/weather-station/.claude
suggest way to forward local setup too

## update to 4.3.0 (maybe also the main docker)
4.3.0 is out

## Review of ADRS
Help to review one adr after another ask question simplify and make teh goals clearer from them
### ADR-003-sensor-event-data-model.md
ADR-003-sensor-event-data-model.md -> add the informatin that the sensor_uid can be used to query more additional sensor infomraiton,
like e.g sclaing factor to convert from q31 to float, discuss it in more detail with the user.
as remote sensors also can use sensor_uids usings sensor_uid in dts is not best option and we need ot extend dts for each file or some remote sensor, a tiny wrapper probably for this and some logic to ensure each unique is really unique (or some linker magic)
e.g goal is if i have a temperature sensor a, i can use some function like
float sensor_get_scalingfactor(uid), and we get the correct one.
### ADR-004-trigger-driven-sampling.md
maybe simplify and focus on decision instead of what was rejected add only most important do not dos.
003 and 004 have an overlap can we seperate them better or things which are duplicated into an own adr?
### ADR-006-lora-channel-boundary.md
lora is at early design phase, letÄs focus here only when we get there
### ADR-008-kconfig-app-composition.md
very specific tings can life in the application but library approach is the preferd one
### ADR-009-native-sim-first.md
copy renoe part to backlog focus on current state for now
### ADR-010-ci-and-dev-environment.md
update as needd compare with claude.md, readme.md and what adr point is and ensure it contains important paths
