# Backlog

## [HTTP-DASHBOARD] Decouple HTML/JS from C source via LittleFS

The current implementation embeds the dashboard HTML, CSS, and Chart.js glue code as C string
literals in `lib/http_dashboard/src/http_dashboard.c`. This makes web asset editing difficult
and prevents live-reload workflows.

**Goal:** serve static assets (HTML, CSS, JS) from LittleFS. The C layer becomes an HTTP router
only. Web assets live in a dedicated source directory, compiled into a filesystem image at build
time.

**Acceptance:**
- No HTML/CSS/JS in `.c` files; all web assets in a dedicated directory.
- Build produces a LittleFS image mounted at a known path.
- HTTP handler reads files from the filesystem; responds with `404` for missing assets.
- Existing endpoints (`/`, `/config`, `/api/data`, `/api/config`) continue to work.

Reference: ADR-011 §Design goal.

---

## [SERIALIZATION] Choose and implement cross-device serialisation format

`env_sensor_data` is an in-memory zbus message, not a wire format.  When
sensor events need to cross a device boundary (LoRa, MQTT, BLE, USB) a proper
encoding layer is required.

**Candidates to evaluate:**
- **Protocol Buffers (nanopb)** — compact binary, schema-enforced, good MCU
  support via nanopb; adds a code-generation step
- **CBOR** — schemaless binary, self-describing, Zephyr has a built-in encoder
  (`zephyr/net/buf.h` + zcbor); no code-gen step
- **Custom fixed layout** — simple, zero overhead, but brittle across firmware
  versions

**Decision criteria:** wire size on LoRa (≤ 20 bytes target per reading),
toolchain integration, versioning story, multi-language decode (Python gateway).

**Acceptance:** an ADR documents the choice; a `lib/sensor_codec` library
encodes/decodes `env_sensor_data` to/from the chosen format; unit tests cover
round-trip correctness.

Reference: ADR-003 §Serialisation, ADR-006.

## ~~[DISPLAY] Verify and merge LVGL SDL display~~ ✓ DONE

Merged via PR #3 (`feat/lvgl-sdl-display`). LVGL heap OOM fix merged via PR #4 (`fix/lvgl-heap-oom`).
`lib/lvgl_display` live on `master`; heap pool bumped to 64 KB.

## ~~Claude memory setup~~ ✓ DONE

Memory system active at `/home/zephyr/.claude/projects/-home-zephyr-workspace/memory/`.
`CLAUDE.md` references the correct project paths.

## ~~update to 4.3.0~~ ✓ DONE

`west.yml` pins `revision: v4.3.0`.

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
