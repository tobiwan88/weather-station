# Backlog

## [HW-SENSOR] SEN0460 functional driver

The SEN0460 PM2.5 sensor library (`lib/sen0460_sensor`) is a stub that proves
the architecture. A functional driver needs:
- I2C communication protocol implementation
- PM2.5 data decoding
- Integration with `hw_sensor_publish()` and `sensor_event_chan`
- DT binding already exists at `dts/bindings/sensor/dfr,sen0460.yaml`

## [Lora SENSOR] Do we support RCP/FOTA coommands?
- review and adjust

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

## [ADR-015-LORA-RPC-RETRY] Implement RPC command retry logic

ADR-015 requires ACK-based RPC delivery with up to 3 retries and SF-dependent
timeout (5s at SF7, 30s at SF10). Currently `lora_radio_rpc_send()` sends a
single frame with `LORA_FLAG_ACK_REQ` but has no retry loop or response listener.

**Acceptance:**
- `lora_radio_rpc_send()` blocks until RPC_RESP received or retries exhausted
- Retry count configurable via Kconfig (default 3)
- Timeout per retry based on spreading factor
- One pending RPC per node (serialized)

Reference: ADR-015 §Reliability model.

---

## [ADR-015-LORA-FOTA-WINDOW] Implement windowed FOTA chunk protocol

ADR-015 requires a windowed ACK protocol with 4–16 chunks in flight, 2s retry
timer, and 3 retries per chunk. Currently `lora_handle_fota_chunk()` processes
one chunk at a time with a broken ACK (offset mismatch reports success).

**Acceptance:**
- Gateway sends 4–16 FOTA_CHUNK frames before waiting for ACKs
- ACK carries written offset, expected offset, and status
- Retry timer (2s) resends unacknowledged chunks
- 3 retries per chunk before aborting

Reference: ADR-015 §Reliability model, §FOTA over LoRa.

---

## [ADR-015-LORA-PERSIST] Persist session immediately after pairing

After `lora_session_add()` in `lora_handle_prov_beacon()`, the session exists
only in RAM. A reboot before the next `settings_save()` call loses the pairing.

**Acceptance:**
- `lora_session_persist()` called after successful pairing
- Sensor node can communicate after gateway reboot without re-pairing

Reference: ADR-015 §Security, §Provisioning.

---

## [ADR-015-LORA-ED25519-CACHE] Cache Ed25519 PSA key instead of import/destroy per beacon

The gateway's Ed25519 private key is imported into PSA Crypto on every
PROV_BEACON reception and destroyed after signing. For a static key, this
should be imported once at init and cached as a `psa_key_id_t`.

**Acceptance:**
- Ed25519 key imported once in `lora_radio_init()` or prov handler init
- `psa_key_id_t` cached as static variable
- `psa_destroy_key()` only called on shutdown (if at all)

Reference: ADR-015 §Security.

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
