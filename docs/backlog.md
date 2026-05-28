# Backlog

## [REQ-INGESTION] Requirements ingestion and CocoIndex Zephyr corpus

See plan: [`docs/requirements/PLAN-requirements-and-cocoindex.md`](requirements/PLAN-requirements-and-cocoindex.md)

Tracks: CocoIndex Layer 2 (Zephyr corpus indexer), REQ file creation per domain (SENSORS, LORA, MQTT, HTTP, DISPLAY, FOTA, POWER, TIME, CONFIG, LOCATION, DATA), MCP config updates.

## [LORA-SECURITY-KEY-CACHE] Security review of cached Ed25519 key in PSA Crypto

The Ed25519 gateway signing key is cached as a persistent `psa_key_id_t` after import-at-init. This review assesses the security implications:

- Does PSA Crypto isolate the key material sufficiently when the handle is held open for the device lifetime vs. the previous import→use→destroy pattern?
- Is the static development key (`gateway_ed25519_sk`) acceptable to hold in flash, and what is the path to production key rotation?
- Audit: no key material leaks via logs or zbus channels.

Reference: ADR-015 §Security.

---

## [LORA-RPC-COMMANDS] Implement remaining RPC commands

The RPC dispatch table in `lora_handler_rpc.c` handles ping/version/reboot. The following commands defined in ADR-015 are not yet implemented on either side:

| Command | Gateway sender | Sensor receiver |
|---------|---------------|-----------------|
| GET_STATUS (0x02) | Not implemented | Not implemented |
| SET_PUBLISH_INTERVAL (0x10) | Not implemented | Not implemented |
| SET_SPREADING (0x11) | Not implemented | Not implemented |
| SET_TX_POWER (0x12) | Not implemented | Not implemented |
| SET_KEEPALIVE (0x13) | Not implemented | Not implemented |
| SET_CHANGE_THRESHOLD (0x14) | Not implemented | Not implemented |
| TRIGGER_SAMPLE (0x20) | Not implemented | Not implemented |
| FOTA_START (0x30) | Not implemented | Not implemented |
| FOTA_CANCEL (0x31) | Not implemented | Not implemented |

**Prerequisite:** LoRa reliable TX (Phases 2-4 from `docs/superpowers/specs/2026-05-27-lora-reliable-tx-design.md`) must be complete so these commands can use `lora_radio_rpc_send()` / `lora_radio_rpc_send_async()`.

**Acceptance:** All 9 commands implemented on both gateway and sensor sides. Each command has a unit test. Integration test validates end-to-end PING → GET_STATUS → SET_PUBLISH_INTERVAL → TRIGGER_SAMPLE → FOTA_START → FOTA_CANCEL → REBOOT.

Reference: ADR-015 §RPC command set.

---

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

## [DOCU-Improvement-Diagramgs] Improve the usage of diagrams in the documentation
- make mermaid diagrams more interactive in webpage build
- add diagrams also in the right pages as inlcude directly (build html from markdown)
- ensure skill exists to create good diagrams

---

## [RENODE-PHASE2] Multi-node simulation with Renode

**Status: Single-node Renode implemented (Phase 2 partial).** Single-node boot,
shell, FOTA, and trace tests run in CI. The remaining work is multi-node
(sensor node + gateway in two Renode machines with shared virtual radio).

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
- CI job `renode` includes multi-node test cases.
- Robot Framework test asserts end-to-end sensor event flow.

Reference: ADR-009 §Future phases, ADR-007.

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

**Prerequisite:** Ethernet + DHCP on `frdm_mcxn947` must be stable and tested
end-to-end so that the HTTP upload path is a reliable replacement before the
UART fallback is removed.

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

## [FOTA-STREAM] Create generic upstreamable stream abstraction

The FOTA sender hardcodes `PM_MCUBOOT_SECONDARY` as the firmware source. This
blocks native_sim testing (no flash partitions) and prevents sensor-specific
firmware images from being stored independently.

**Goal:** A generic, reusable stream vtable that any module can use to read
from flash, RAM, or any other backing store. Designed for upstreamability to
Zephyr.

**Implementation:**
- `lib/fota_stream/` — new library, Kconfig-gated, SYS_INIT-wired, no heap.
- Vtable interface:
  ```c
  struct fota_stream {
      int    (*read)(struct fota_stream *s, uint32_t offset, uint8_t *buf, size_t len);
      size_t (*size)(struct fota_stream *s);
      int    (*write)(struct fota_stream *s, uint32_t offset, const uint8_t *data, size_t len);
      int    (*flush)(struct fota_stream *s);
      void   (*close)(struct fota_stream *s);
  };
  ```
- Backends:
  - `fota_stream_flash` — wraps `FIXED_PARTITION_ID()` + `flash_area_read/write()`.
    Configurable partition via Kconfig.
  - `fota_stream_ram` — wraps a static `uint8_t buf[N]` where N =
    `CONFIG_FOTA_STREAM_RAM_SIZE`. For native_sim testing and small images.
- Kconfig: `CONFIG_FOTA_STREAM`, `CONFIG_FOTA_STREAM_FLASH`,
  `CONFIG_FOTA_STREAM_RAM`, `CONFIG_FOTA_STREAM_RAM_SIZE`.

**Acceptance:**
- Both backends compile and pass unit tests on native_sim.
- `lora_handler_fota.c` sender uses `fota_stream->read()` instead of
  `flash_area_read()`.
- FOTA sender testable on native_sim using RAM backend.

Reference: PR #49 FIXME comments at `flash_area_read` and `flash_img_init` in
`lora_handler_fota.c`.

---

## [FOTA-IMAGE-REGISTRY] Create image registry mapping sensor UIDs to firmware streams

Different sensor nodes (different hardware, sensor types) need different
firmware images. Currently there is no way to associate a specific firmware
image with a specific target sensor UID.

**Goal:** A static registry that maps sensor UIDs to `fota_stream` instances,
tracking upload state (empty/uploading/ready).

**Implementation:**
- `lib/fota_image_registry/` — new library, Kconfig-gated.
- Static array sized by `CONFIG_FOTA_IMAGE_REGISTRY_MAX` (default 4).
- Entry:
  ```c
  struct fota_image_entry {
      uint8_t  label[16];           /* e.g. "STM32WLE5-v1.2" */
      uint32_t target_uid;          /* 0 = any, otherwise must match sensor UID */
      enum fota_image_state state;  /* EMPTY, UPLOADING, READY */
      size_t   image_size;
      struct   fota_stream *stream;
  };
  ```
- API: `upload_start`, `upload_chunk`, `upload_finish`, `get(index)`,
  `find_for_uid(uid)`.

**Acceptance:**
- Multiple images can be uploaded and stored independently.
- `fota_image_registry_find_for_uid(uid)` returns the best-matching image.
- Unit test: upload two images, verify lookup by UID.

Reference: `lora_fota_event.image_index` field added in PR #49.

---

## [FOTA-SENDER-STREAM] Refactor FOTA sender to use fota_stream + image registry

Replace hardcoded `PM_MCUBOOT_SECONDARY` in `lora_handler_fota.c` sender with
stream-based image selection from the registry.

**Goal:** FOTA sender reads from a `fota_stream` selected by `image_index` from
the event, enabling sensor-specific firmware and native_sim testing.

**Implementation:**
- `fota_start_handler` looks up stream from `fota_image_registry_get(image_index)`.
- Replace `flash_area_read(fota_sender.fota_ctx.fap, ...)` with
  `stream->read(stream, ...)`.
- Remove `struct flash_img_context fota_ctx` from `fota_sender` (no longer needed
  for reading).
- `image_index=0` defaults to the first image in the registry (backward compat).

**Acceptance:**
- FOTA sender works with both flash and RAM backends.
- `lora_fota_event.image_index` correctly selects the target image.
- Native_sim integration test passes with RAM backend.

Depends on: [FOTA-STREAM], [FOTA-IMAGE-REGISTRY].

---

## [FOTA-HTTP-SENSOR-UPLOAD] Add HTTP endpoints for sensor firmware image management

Add authenticated HTTP endpoints to `lib/http_dashboard` for uploading and
managing sensor firmware images.

**Goal:** Browser/CLI can upload sensor firmware images to the gateway, list
stored images, and trigger FOTA to specific sensor nodes.

**Implementation:**
- `POST /api/fota/sensor/upload` — streams image data to a new registry entry.
  Auth: same session/bearer pattern as gateway FOTA upload.
- `GET /api/fota/sensor/images` — returns JSON list of stored images
  (label, target_uid, state, image_size).
- `POST /api/fota/sensor/<uid>/apply` — publishes `FOTA_START` on
  `lora_fota_chan` with the matching `image_index` from the registry.

**Acceptance:**
- End-to-end: upload image → list shows it → apply triggers FOTA → sensor ACKs.
- Auth: unauthenticated requests rejected with 401.
- Concurrent uploads serialised (one at a time, like gateway FOTA upload).

Depends on: [FOTA-IMAGE-REGISTRY].

---

## [FOTA-PIPELINE] Enable true window pipelining (>1 chunk in flight)

The current `lora_pending.c` allows only one pending slot per node. This caps
the FOTA window at 1 (sequential ACK per chunk) even though the protocol
supports 4–16 chunks in flight.

**Goal:** Enable true window pipelining for faster FOTA transfers.

**Options:**
- **A:** Expand `lora_pending.c` to support N slots per node (FOTA_WINDOW).
  Higher complexity — changes to core pending subsystem.
- **B:** FOTA sender manages its own retry ring, bypassing pending subsystem
  for chunk delivery. Medium complexity — some code duplication.

**Acceptance:**
- Window size > 1 confirmed working (e.g., 4 chunks in flight).
- FOTA transfer time reduced proportionally.
- Unit test validates pipelined ACK processing.

Depends on: [FOTA-STREAM] (for native_sim testing).

---

## [FOTA-INTEGRATION-TEST] End-to-end FOTA relay integration test

Pytest-based integration test exercising the full FOTA relay path: gateway
sends chunks → sensor node receives, writes, ACKs → gateway advances.

**Goal:** Automated CI test validating the FOTA protocol end-to-end using the
fake LoRa driver loopback and RAM-backed stream.

**Implementation:**
- `tests/integration/pytest/test_lora_fota.py` — new test file.
- Tests:
  - `fota_chunk_receive_and_ack` — single chunk round-trip.
  - `fota_full_transfer_small` — small image (1 KB) fully transferred.
  - `fota_chunk_timeout_retry` — retry on missing ACK.
  - `fota_cancel_mid_transfer` — cancel aborts sender, resets receiver.
- Requires sensor node subprocess (like `test_sensor_node_gateway.py`).

**Acceptance:**
- All 4 tests pass on native_sim.
- FOTA completes within timeout (≤30s for 1 KB image).

Depends on: [FOTA-STREAM].

---

## [RENODE-CI-DOCKER] Pre-built CI Docker image with Renode

The `renode` CI job downloads the portable Renode tarball each run (~30 s).
A pre-built CI image would eliminate this delay and decouple from GitHub
release availability.

**Goal:** Create and publish `ghcr.io/tobiwan88/weather-station-ci:latest`,
extending `zephyr_docker:arm` with Renode and Robot Framework pre-installed.

**Implementation:**
- Build a new Docker image based on `.devcontainer/Dockerfile.ci` that adds
  the Renode portable release (ARM64 for native runner, x86_64 for CI) and
  `robotframework==6.1`.
- Publish to GitHub Container Registry.
- Update `.github/workflows/ci.yml` `renode` job to use the custom image
  instead of downloading Renode inline.

**Acceptance:** Renode CI job runs without the 30 s download step. Cold build
time drops from ~60 s to ~15 s.

---

## [RENODE-ENET-QOS] Renode ENET-QoS peripheral model

The FRDM-MCXN947 uses `nxp,enet-qos` Ethernet, which has no Renode peripheral
model. This blocks HTTP FOTA testing and network-dependent features (MQTT,
HTTP dashboard, SNTP) in Renode simulation.

**Goal:** Add an `enet-qos` peripheral model to Renode (or contribute to
upstream), enabling network-based Renode testing of the full gateway stack.

**Acceptance:**
- Renode `.repl` includes a functional ENET-QoS peripheral at `0x40100000`.
- `gateway_fota.robot` passes with HTTP FOTA test case.
- The model is contributed upstream or maintained as a project patch.

---

## [RENODE-SPI-NOR] Functional SPI NOR flash model for Renode

The Renode `.repl` models the MCXN947 external W25Q64 flash as bare
`Memory.MappedMemory`, which cannot respond to SPI IP commands (JEDEC ID read,
SFDP, erase, page program). This forces a Renode-specific Kconfig fragment that
disables `CONFIG_FLASH_MCUX_FLEXSPI_NOR` and a DTS overlay to disable
`ext_flash_ctrl`.

**Goal:** Replace `Memory.MappedMemory` with a functional SPI NOR flash
peripheral model so the full gateway firmware (with external flash) boots in
Renode without workarounds.

**Implementation:**
- Check if Renode v1.16.1 has a `SPI.NORFlash` or equivalent model.
- If yes, update `simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl` to use it.
- If no, implement or contribute one.
- Remove `frdm_mcxn947_mcxn947_cpu0_renode.conf` and `_renode.overlay`.

**Acceptance:** Gateway firmware builds without the Renode-specific workarounds
and boots to shell in Renode, with external flash accessible via FlexSPI.

---

## [RENODE-TRACE-GH-PAGES] Perfetto deep-link 404 guard and retention cleanup

The `deploy-results` CI job deploys `trace_combined.json` (Chrome Tracing
format) and test reports to the `gh-pages` branch. The Perfetto UI deep-link
pattern `ui.perfetto.dev/#!/?url=...` works for any public URL.

**Known gaps:**
- No 404 guard: if a trace file was not generated (build failure, size > 50 MB),
  the deep-link silently fails in Perfetto UI. A health-check script or index
  page listing available traces would improve UX.
- No retention cleanup: old PR traces accumulate indefinitely on `gh-pages`.
  A periodic cleanup job (e.g., remove traces for closed PRs older than 30
  days) is needed.
- Trace format: currently outputs Chrome Tracing JSON. Migrating to the binary
  Perfetto proto format (.perfetto or .pb) would reduce file size for large
  traces and enable Perfetto's full feature set (flow events, counters).

**Acceptance:**
- An index page at `<org>.github.io/weather-station/dev/traces/` lists all
  available traces with links.
- Old PR trace directories are removed when the PR is closed/merged.
- Optional: binary Perfetto proto output from parse-trace.py.
