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

## [FOTA-CI-KEY] Suppress signing key from CI logs

The CI snippet in `docs/architecture/firmware-update.md` uses
`echo "$MCUBOOT_SIGN_KEY" > keys/dev-ed25519.pem`, which may echo the key
value in the build log depending on the CI runner's debug settings.

**Goal:** Key material never appears in CI logs.

**Implementation:**
- Replace `echo "$MCUBOOT_SIGN_KEY"` with
  `printf '%s' "$MCUBOOT_SIGN_KEY"` (no trailing newline, no shell trace echo)
  or write via a helper script that sets `+x` before writing.
- Update the CI YAML snippet and the documentation in
  `docs/architecture/firmware-update.md`.
- Verify with `set -x` trace that no key bytes appear in stdout.

Reference: ADR-014 §Key management.

---

## [FOTA-REMOVE-UART-MCUMGR] Remove MCUmgr UART transport; HTTP is the standard update path

`apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf` currently enables
`CONFIG_MCUMGR_TRANSPORT_UART`, `CONFIG_MCUMGR_GRP_IMG`, and
`CONFIG_MCUMGR_GRP_OS` so that firmware can be uploaded over the shared
`flexcomm4_lpuart4` UART. This adds code size and complexity for a transport
that is redundant with the HTTP upload path.

**Goal:** HTTP (`POST /api/fota/upload`) is the single standard update path.
UART MCUmgr is removed. Physical recovery (bricked device) uses the NXP ROM
ISP bootloader or JTAG re-flash — not MCUmgr.

**Implementation:**
- Remove MCUmgr Kconfig from `frdm_mcxn947_mcxn947_cpu0.conf`:
  `CONFIG_MCUMGR`, `CONFIG_MCUMGR_TRANSPORT_UART`, `CONFIG_MCUMGR_GRP_IMG`,
  `CONFIG_MCUMGR_GRP_OS`.
- Keep `CONFIG_IMG_MANAGER`, `CONFIG_STREAM_FLASH`, `CONFIG_FLASH_MAP` — still
  required by the HTTP upload handler.
- Update ADR-014 and `docs/architecture/firmware-update.md` to document HTTP
  as the sole transport and JTAG/ISP as the recovery path.
- Verify `CONFIG_ZVFS_POLL_MAX` can revert to 8 without MCUmgr's extra socket.

Reference: ADR-014.

---

## [FOTA-CONFIRM-HEALTH-CHECKS] Replace time-based confirm with iterable health-check pattern

`lib/fota_confirm` currently confirms the image after a fixed settle delay
(`CONFIG_FOTA_CONFIRM_DELAY_S`). This is a blunt instrument: any library that
can hang silently would still allow confirmation to proceed.

**Goal:** Image confirmation requires all registered health checks to return
`ok` before `boot_write_img_confirmed()` is called. Libraries opt in by
registering a health-check callback; the confirm library polls them.

**Implementation:**
- Define an iterable section entry:
  ```c
  struct fota_health_check {
      const char *name;
      int (*check)(void);   /* returns 0 if healthy, negative errno if not */
  };
  #define FOTA_HEALTH_CHECK_DEFINE(name, fn) \
      STRUCT_SECTION_ITERABLE(fota_health_check, name) = { .name = #name, .check = fn }
  ```
- `fota_confirm_init` iterates `STRUCT_SECTION_START/END(fota_health_check)`;
  if any check returns non-zero at the time of evaluation, reschedule and retry
  (up to a `CONFIG_FOTA_CONFIRM_MAX_RETRIES` limit before giving up and letting
  MCUboot roll back).
- HTTP dashboard, MQTT publisher, SNTP can each register a check
  (`http_dashboard_is_ready()`, `mqtt_is_connected()`, etc.) under
  `CONFIG_FOTA_CONFIRM_CHECK_*` guards.
- Expose `fota_confirm_now()` as a public API for callers that want to trigger
  confirmation synchronously once they know the system is healthy.
- Remove the fixed delay as the sole gate; keep a minimum floor
  (`CONFIG_FOTA_CONFIRM_MIN_DELAY_S`) to let sockets bind before the first poll.

Reference: ADR-014 §Image confirmation; ADR-008 §iterable sections pattern.

---
