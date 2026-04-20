# Backlog

## [ADR-008-RULE4] Move lvgl_display_run() out of gateway/main.c

`apps/gateway/src/main.c` violates ADR-008 Rule 4 by calling `lvgl_display_run()`
(a blocking SDL timer loop) under `#if CONFIG_LVGL_DISPLAY`. On Linux (devcontainer /
CI target) SDL2 works fine from any thread, so the "must run on main thread" constraint
does not apply.

**Goal:** `main.c` becomes `LOG_MODULE_REGISTER + return 0`, conforming to Rule 4.

**Implementation:**
- In `lib/lvgl_display/src/lvgl_display.c`: rename `lvgl_display_run()` to a static
  thread entry `lvgl_thread_fn()`; call `create_ui()` + `display_blanking_off()` at
  thread start, then loop `lv_timer_handler()` with `k_msleep()`.
- Start the thread from `lvgl_display_init()` (already runs via `SYS_INIT APPLICATION 91`).
  Use `K_THREAD_DEFINE` with a new `CONFIG_LVGL_DISPLAY_STACK_SIZE` Kconfig (default 8192).
- Remove `lvgl_display_run()` declaration from
  `lib/lvgl_display/include/lvgl_display/lvgl_display.h`.
- Simplify `apps/gateway/src/main.c` to remove the `#if CONFIG_LVGL_DISPLAY` block.

**Acceptance:** `/build-and-test` passes; LVGL window opens on native_sim; `main.c`
contains only `LOG_MODULE_REGISTER` + `return 0`.

---

## [ADR-008-REVIEW] Review main.c files exceeding 50-line rule

Both app `main.c` files exceed the ADR-008 50-line rule:
- `apps/gateway/src/main.c`: 77 lines — contains a zbus listener callback with
  sensor event logging logic
- `apps/sensor-node/src/main.c`: 66 lines — same pattern

**Goal:** Assess whether the excess logic qualifies as app-specific policy (and
is therefore acceptable per the "When app-level C code is acceptable" section)
or should be extracted to a library.

**Acceptance:**
- Decision documented: either the ADR's 50-line limit is adjusted for these
  specific cases with justification, or the logic is extracted to a library.

Reference: ADR-008 §`main.c` 50-line rule.

---

## [ADR-010-DEVCONTAINER] Align devcontainer and CI on single approved image

**Status: BLOCKED** — remote container build currently does not work.

`devcontainer.json` uses a local `Dockerfile` with `FROM dev:latest` instead
of pulling `ghcr.io/tobiwan88/zephyr_docker:latest` directly. This diverges
from the ADR-010 requirement that local and CI use the same image.

**Goal:** Decide between:
- **Option A (remote image):** `devcontainer.json` references
  `ghcr.io/tobiwan88/zephyr_docker:latest` directly — no local Dockerfile.
  Requires the remote image build to be working.
- **Option B (local build):** Keep the local `Dockerfile` but base it on the
  approved image (`FROM ghcr.io/tobiwan88/zephyr_docker:latest`) and update
  ADR-010 to document the local-build approach.

Additional CI gaps to fix alongside:
- Add `ZEPHYR_BASE=/home/zephyr/workspace/zephyr` prefix to all `west build`
  and `west twister` commands in `.github/workflows/ci.yml`.
- Replace `native_sim` shorthand with `native_sim/native/64` in CI matrix and
  test job.
- Add `zephyr-checkpatch-diff` hook to `.pre-commit-config.yaml`.
- Add `ci-success` aggregator job to block PR merge on failure.

Reference: ADR-010.


---

## [SENSOR-REGISTRY-SCALING] Extend sensor_registry with per-UID scaling metadata

`sensor_registry` currently maps UID → label/location. It needs to also carry
the Q31 physical range (`range_min`, `range_max`) for each sensor variant so
that consumers can decode Q31 values without assuming a fixed global formula.

**Goal:** `sensor_registry_get_scaling(uid)` returns the correct decode
parameters for any sensor, regardless of its physical range variant.
Example: an industrial temperature sensor covering −200 °C to +500 °C uses
the same `SENSOR_TYPE_TEMPERATURE` enum as an indoor sensor, but registers
different scaling.

**Acceptance:**
- `sensor_scaling_t` struct with `range_min`, `range_max` (double).
- `sensor_registry_register()` accepts a `sensor_scaling_t` parameter.
- `sensor_registry_get_scaling(uid)` returns the registered scaling.
- `q31_to_physical(q31, range_min, range_max)` replaces type-specific decode helpers in display/MQTT code.
- Fake sensors register their default scaling at `SYS_INIT` time.
- Unit test: register two sensors of the same type with different ranges; verify independent decode.

Reference: ADR-003 §Q31 encoding, §sensor_uid contract.

---

## [SENSOR-REGISTRY-REMOTE-UID] Persist remote sensor UIDs via Zephyr settings backend

Remote sensors (LoRa, BLE) cannot be pre-assigned UIDs in the gateway's
devicetree. Their UIDs must be generated at first registration and persisted
across gateway reboots so that display routing and MQTT topics remain stable.

**Goal:** `sensor_registry_register_remote(node_id, type, scaling, &uid)` assigns
a pseudo-random UID on first call, saves it via `CONFIG_SETTINGS`, and returns
the same UID on subsequent calls for the same `node_id`.

**Acceptance:**
- UID generated in range `0x1000–0xFFFF`; collision-checked against all known UIDs.
- Settings key format: `sens/rem/<node_id_hex>` → `uint32_t` UID.
- UID survives gateway reboot (settings loaded at boot).
- `native_sim` uses a RAM-backed settings stub (no flash required).
- Build-time check or documentation enforces that local UIDs stay in `0x0001–0x0FFF`.
- Unit test: register same node_id twice; assert same UID returned; assert no collision with local UIDs.

Reference: ADR-003 §UID assignment — remote sensors.

---

## [RENODE-PHASE2] Multi-node simulation with Renode

Once the native_sim architecture is validated end-to-end and an MCU is selected
(ADR-007 trigger), move to Renode for multi-node integration tests with a
virtual LoRa radio medium.

**Goal:** sensor node and gateway run as two separate Renode machines connected
via a simulated wireless medium. End-to-end LoRa packet exchange tested
automatically in CI without physical hardware.

**Prerequisites:**
- MCU selection confirmed (ADR-007)
- `lib/lora_radio` native_sim stub replaced with a Renode platform description
- Board definitions written for the chosen MCU

**Acceptance:**
- `simulation/multi_node.resc` launches both nodes with a shared virtual radio medium.
- CI job `renode-integration` (currently disabled in pipeline) passes.
- Robot Framework test `simulation/weather_test.robot` asserts end-to-end sensor event flow.

Reference: ADR-009 §Future phases, ADR-007.

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

---

## [Remote sensor manager] Thread maybe more busy as eneded

- we use zbus + timeout and drain afterwards. Normal operation we should not have to many events at once. Maybe enough just to put events on simple message que or other format?

## [HTTP Dashboard] User login to access configuraiton dashboard

- Instead of posting the token there shall be a small user webpage to loging to the configuration, for dev we use admin: admin
- the user can change the password and username

## [MQTT Config] MQTT can be configured
- can be enabled/disabled
- user can change address, authentication
