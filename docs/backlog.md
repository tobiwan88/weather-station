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

## [LORA-BRIDGE-APP] Create `apps/lora_bridge` — Wio-E5 Mini LoRa co-processor firmware

The gateway's LoRa radio runs on a dedicated Wio-E5 Mini (STM32WLE5JC) co-processor
connected via UART (Flexcomm5 on the MCXN947). This app runs `lib/lora_radio/` on
the co-processor and forwards decoded `env_sensor_data` frames to the gateway over UART.

**Goal:** A Zephyr application targeting the Wio-E5 Mini that runs the full LoRa protocol
(AES-128-GCM, Ed25519 pairing, RPC, FOTA-over-LoRa) and communicates with the gateway
via a UART zbus proxy agent.

**Prerequisites:**
- Custom Zephyr board definition for Wio-E5 Mini (adapted from `nucleo_wl55jc`)
- UART proxy agent for zbus channel bridging (deferred per ADR-015)
- `lib/lora_radio/` already implements the protocol

**Implementation:**
- Create `apps/lora_bridge/` with `prj.conf`, `CMakeLists.txt`, `src/main.c`
- Create `boards/wio_e5_mini/` with DTS, pinctrl, Kconfig
- Board overlay: enable SubGHz SPI (SX126x), UART1 for gateway communication,
  RF switch GPIOs (PA4=RF_CTRL1, PA5=RF_CTRL2), LED on PB5
- Kconfig: `CONFIG_LORA_RADIO=y`, `CONFIG_LORA_RADIO_FAKE=n`,
  `CONFIG_UART_LORA_BRIDGE=n`, sensor drivers disabled
- UART framing: send 24-byte `env_sensor_data` wire frames to gateway
  (same format as `lib/uart_lora_bridge/` expects: magic 0x5A 0xA5, length, payload, CRC8)
- `src/main.c` = `LOG_MODULE_REGISTER + return 0` (ADR-008 Rule 4)

**Acceptance:**
- Builds for `wio_e5_mini` target
- LoRa radio initializes with SX126x driver (EU868, SF10, BW125)
- UART communication with gateway verified (loopback test)
- Renode simulation with two Wio-E5 machines (deferred to RENODE-PHASE2)

Reference: ADR-015 §Physical topology, docs/hardware/README.md §Gateway — Wio-E5 Mini.

---

## [OUTDOOR-SENSOR-NODE-APP] Create `apps/outdoor_sensor_node` — Wio-E5 Mini sensor node firmware

The outdoor sensor node is a self-contained Zephyr device built around the Wio-E5 Mini
(STM32WLE5JC). It reads local sensors (BME688 + SEN0460) and transmits compact 5-byte
readings over LoRa P2P to the gateway.

**Goal:** A battery-powered Zephyr application targeting the Wio-E5 Mini that reads
environmental sensors and transmits via LoRa with ultra-low power sleep between cycles.

**Prerequisites:**
- Custom Zephyr board definition for Wio-E5 Mini (shared with `apps/lora_bridge`)
- `lib/lora_radio/` already implements the protocol
- I2C sensor drivers (BME680 via Zephyr upstream, SEN0460 needs custom driver)

**Implementation:**
- Create `apps/outdoor_sensor_node/` with `prj.conf`, `CMakeLists.txt`, `src/main.c`
- Reuse `boards/wio_e5_mini/` from lora_bridge app
- Board overlay: enable I2C2 (PA11=SDA, PA12=SCL) for sensors, SubGHz SPI for LoRa,
  RF switch GPIOs, disable UART1 (no gateway UART needed on sensor node)
- Kconfig: `CONFIG_LORA_RADIO=y`, `CONFIG_SENSOR=y`, `CONFIG_BME680=y`,
  `CONFIG_FAKE_SENSORS=n`, `CONFIG_UART_LORA_BRIDGE=n`
- SEN0460 driver: custom Zephyr sensor driver for DFRobot PM2.5 sensor (I2C addr 0x19)
- Sensor trigger pattern: subscribe to `sensor_trigger_chan`, read sensors, publish to
  `sensor_event_chan` (same pattern as `lib/fake_sensors/`)
- Power management: WOR sleep mode (2.1 µA), configurable publish interval

**Acceptance:**
- Builds for `wio_e5_mini` target
- BME688 and SEN0460 sensors read via I2C
- LoRa data frames transmitted to gateway (SF10, BW125, EU868)
- Power consumption: ≤5 µA average with 60s publish interval
- End-to-end test: gateway receives sensor events on `sensor_event_chan`

Reference: ADR-015 §Compact sensor data encoding, docs/hardware/README.md §Outdoor Sensor Node 1.

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
