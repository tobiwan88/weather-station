# Backlog

Open work only. Done items live in git history.

## LoRa

- **LORA-RELIABLE-TX**: reliable RPC send (`lora_radio_rpc_send()` / `_async()`) with
  ACK, retry and one pending RPC per node. Prerequisite for the RPC commands below.
- **LORA-RPC-COMMANDS**: `lora_handler_rpc.c` only handles ping/version/reboot. Missing on
  both sides: GET_STATUS (0x02), SET_PUBLISH_INTERVAL (0x10), SET_SPREADING (0x11),
  SET_TX_POWER (0x12), SET_KEEPALIVE (0x13), SET_CHANGE_THRESHOLD (0x14),
  TRIGGER_SAMPLE (0x20), FOTA_START (0x30), FOTA_CANCEL (0x31). Each needs a unit test
  plus one end-to-end integration test.
- **LORA-SECURITY-KEY-CACHE**: review the cached Ed25519 gateway key (persistent
  `psa_key_id_t`): isolation, static dev key in flash, production rotation path, no key
  material in logs or zbus.
- **LORA-BRIDGE-HW**: bring up `apps/lora_bridge` (Wio-E5 Mini co-processor) and
  `lib/uart_lora_sender` on real hardware; board definition and Renode model exist or are
  planned under `boards/` and `simulation/renode/`.

## FOTA

- **FOTA-IMAGE-REGISTRY**: static registry mapping sensor UID to an `io_stream` firmware
  image (empty/uploading/ready), sized by Kconfig. (The stream abstraction itself is done:
  `lib/io_stream`.)
- **FOTA-SENDER-STREAM**: replace hardcoded `PM_MCUBOOT_SECONDARY` in the LoRa FOTA sender
  with stream selection by `image_index`, so it runs on native_sim.
- **FOTA-HTTP-SENSOR-UPLOAD**: authenticated HTTP endpoints to upload, list and trigger
  sensor firmware images.
- **FOTA-PIPELINE**: `lora_pending.c` allows one pending slot per node, capping the FOTA
  window at 1; enable 4-16 chunks in flight.
- **FOTA-INTEGRATION-TEST**: pytest end-to-end FOTA relay test using the fake LoRa loopback
  and a RAM-backed stream.
- **FOTA-CONFIRM-HEALTH-CHECKS**: replace the time-based confirm in `lib/fota_confirm` with
  iterable health-check callbacks; confirm only when all return ok.
- **FOTA-REMOVE-UART-MCUMGR**: drop MCUmgr UART transport from the frdm_mcxn947 gateway
  config once Ethernet/DHCP is stable; HTTP upload becomes the only path.
- **FOTA-CI-KEY**: stop `echo "$MCUBOOT_SIGN_KEY" > keys/...` in the documented CI snippet
  from risking the key in logs (write via file secret or masked step).

## Gateway and libraries

- **LVGL-MAIN-THREAD**: `apps/gateway/src/main.c` still calls `lvgl_display_run()`. Move
  the loop into a thread started from `lvgl_display_init()` (new stack-size Kconfig),
  remove the declaration, and reduce `main.c` to `LOG_MODULE_REGISTER` + `return 0`.
- **SENSOR-REGISTRY-SCALING**: carry per-UID Q31 range (`range_min`/`range_max`) in
  `sensor_registry` with `sensor_registry_get_scaling(uid)`, so decode no longer assumes
  one global formula; fake sensors register defaults; add a unit test.
- **HTTP-DASHBOARD-ASSETS**: move embedded HTML/JS out of C string literals (LittleFS image
  or generated header) so `http_dashboard.c` is routing only.
- **HTTP-DASHBOARD-COUPLING**: remove direct calls from `http_dashboard.c` into other
  subsystems' APIs; use zbus channels or a dedicated conditional file.

## Tooling, CI and docs

- **DEVCONTAINER-IMAGE** (blocked: remote image build broken): make devcontainer and CI use
  `ghcr.io/tobiwan88/zephyr_docker`. Also add a `zephyr-checkpatch-diff` pre-commit hook and
  a `ci-success` aggregator job.
- **REQ-INGESTION**: create remaining REQ files per domain and a CocoIndex Zephyr corpus
  indexer (Layer 2) plus MCP config.
- **DOCS-DIAGRAMS**: make mermaid diagrams more interactive and include them inline on the
  matching pages.

## Renode

- **RENODE-PHASE2**: single-node boot/shell/FOTA/trace tests run in CI. Missing: two
  machines (sensor node + gateway) on a shared virtual LoRa medium, with automated
  end-to-end packet exchange.
- **RENODE-CI-DOCKER**: publish `ghcr.io/tobiwan88/weather-station-ci` (zephyr_docker:arm +
  Renode + Robot Framework) to avoid downloading Renode each CI run.
- **RENODE-ENET-QOS**: add an `nxp,enet-qos` Renode peripheral model so HTTP, MQTT and SNTP
  can be tested on the simulated MCXN947.
- **RENODE-SPI-NOR**: functional SPI NOR model for the W25Q64 (JEDEC ID, SFDP, erase,
  program) to drop the Renode-specific Kconfig/DTS workarounds.
- **RENODE-TRACE-GH-PAGES**: guard Perfetto deep-links against missing/oversized trace files
  and add retention cleanup for old traces on gh-pages.
