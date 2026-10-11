# Design decisions

Short list of decisions that still constrain the code. Numbers match the old
`ADR-0NN` references in code comments. The full historical ADR text (context,
alternatives, consequences) is in git history: `git log -- docs/adr`.

## ADR-001 Repo and west workspace
West T2 topology: this repo is both the west manifest and a Zephyr module
(`zephyr/module.yml`). Zephyr and external modules are fetched by west, never
committed. Keep `name-allowlist` in `west.yml` minimal.

## ADR-002 zbus is the only IPC
One concern = one channel. `ZBUS_CHAN_DECLARE` in the library's public header,
`ZBUS_CHAN_DEFINE` in exactly one `.c`. Producers never know consumers.
Listeners run in the publisher's context and must not block; slow work goes in a
subscriber thread. Message structs are copyable (no pointers to heap).

## ADR-003 Sensor event data model
One event = one measurement, in a flat 20-byte `env_sensor_data` with a Q31
value. Encode/decode helpers live in `sensor_event.h`; convert to physical units
only at the display/MQTT/log edges. `sensor_uid` is the identity key; never hardcode it, go through
`sensor_registry`. The struct is not a wire format; cross-device data needs an
explicit encoding layer. No heap.

## ADR-004 Trigger-driven sampling
No sensor manager. Anyone publishes `sensor_trigger_chan`; each driver
subscribes and responds (`target_uid = 0` is broadcast). Sources, sensors and
consumers must stay unaware of each other.

## ADR-005 Fake sensors
`lib/fake_sensors` is a production-quality driver set for native_sim: DT-defined,
shell-controlled, trigger-driven, self-registering via `STRUCT_SECTION_ITERABLE`.
Real hardware replaces it by Kconfig only. Never enabled in production builds.

## ADR-007 Gateway and display share one image
`apps/gateway` contains both gateway and display logic. They talk only via zbus
(no cross includes between display and connectivity libs), so a later split is a
Kconfig change.

## ADR-008 Kconfig-only app composition
Apps select features in `prj.conf` / board `.conf` only. No
`target_link_libraries()` in app CMake. `main.c` is `LOG_MODULE_REGISTER` plus
`return 0`; libraries wire themselves with `SYS_INIT`.

## ADR-009 native_sim first
All development and CI target `native_sim/native/64`; no hardware needed. Renode
(multi-node) and real boards come later. Code must run under simulated time
(avoid `k_busy_wait` assumptions).

## ADR-010 One container image
`ghcr.io/tobiwan88/zephyr_docker` is used for devcontainer and CI. Always set
`ZEPHYR_BASE` explicitly (the image default is stale).

## ADR-011 HTTP dashboard
`lib/http_dashboard`, Kconfig opt-in, port 8080, self-init via `SYS_INIT`.
Ring buffer is guarded by a `k_spinlock` (listeners can run in ISR); copy a
snapshot under the lock and serialise outside it. Needs the
`http_dashboard_sections.ld` linker fragment. Talk to other subsystems via zbus
(or a dedicated file compiled only when both are enabled), not direct calls.
Auth: session cookie for browsers, bearer token for automation
(`CONFIG_HTTP_DASHBOARD_AUTH`). HTML is still embedded as C strings (see backlog).

## ADR-012 Pytest integration tests
`pytest-twister-harness` runs tests against the built binary. Tests use harness
classes (shell, HTTP, MQTT page objects), never raw DUT strings. One DUT boots
per session; auth tests use the `authed_harness` fixture.

## ADR-013 MQTT config via zbus
Runtime MQTT settings go through `config_cmd_chan` (`config/mqtt/` namespace).
The dashboard publishes, `mqtt_publisher` consumes; neither calls the other.

## ADR-014 MCUboot FOTA
Standalone MCUboot (no TF-M) on `frdm_mcxn947/mcxn947/cpu0` via sysbuild, ED25519
signing, SWAP_MOVE. Upload over HTTP (`/api/fota/*`, behind the dashboard auth);
the new image must call `boot_write_img_confirmed()` after a settle delay or
MCUboot reverts. Signing keys live offline / in a git-ignored `keys/`, never in
the repo. Known gaps (home project): no anti-rollback, no encrypted slots, no
hardware key isolation. Hardware-only feature.

## ADR-015 LoRa protocol (supersedes ADR-006)
LoRa details stay inside the LoRa libraries; consumers only see
`env_sensor_data` on `sensor_event_chan` (one packet becomes N events, stamped
with the gateway clock). Frame: 10-byte header (type+version, flags, src, dst,
32-bit seq) plus a 12-byte AES-128-GCM tag (header = AAD; nonce = node id |
direction | seq32, built in `lib/lora_proto`) and CRC16. Sensor data is fire and
forget; RPC and FOTA chunks use ACK plus retry (3 retries, one RPC pending per
node). Pairing is button-triggered with a 30 s window and an Ed25519-signed
response; session keys persist in Settings/NVS. Respect EU868 1 % duty cycle
(publish interval at least 60 s). Wire details: `architecture/lora-protocol.md`.

## ADR-016 Trace recorder
`lib/trace_recorder` overrides Zephyr's `CONFIG_TRACING_USER` hooks to write
8-byte records into a static ring buffer, dumped post-mortem (Renode
`ReadMemory`) for thread-level analysis. Debug/CI feature, off by default.

## ADR-017 io_stream
`lib/io_stream` is a vtable (read/write/seek/tell/size/flush/close) with flash
and RAM-buffer backends so FOTA code runs on native_sim. Static instances only.
Flash backend is sequential-write (non-sequential writes return `-ENOTSUP`).
