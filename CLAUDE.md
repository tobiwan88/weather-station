# weather-station

Zephyr firmware for a weather station. Sensor nodes send readings (temperature, humidity,
pressure, CO2, VOC, PM2.5) over LoRa, UART or a native_sim FIFO to a gateway. The gateway
publishes them on zbus and serves them through an LVGL screen, an HTTP dashboard and MQTT.
Development targets `native_sim/native/64`. Hardware: FRDM-MCXN947 (gateway), LoRa-E5 mini
(outdoor node), Wio-E5 mini (LoRa bridge, board defined in `boards/`).

## Build and test

The build environment is the CI container (Zephyr SDK, west venv). The west workspace root is
the parent of this repo and is mounted at `/home/zephyr/workspace`. From a macOS host, run
everything through `scripts/ws.sh` (it forwards into the `ws_build` container). Commands run
from the workspace root, so app paths are prefixed with `weather-station/`.

```bash
scripts/ws.sh 'west build -p -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'
scripts/ws.sh 'west build -p -b native_sim/native/64 -d build/native_sim_native_64/sensor-node weather-station/apps/sensor-node'
scripts/ws.sh 'cd weather-station && west twister -p native_sim/native/64 -T tests/ -N -j 8'
scripts/ws.sh 'cd weather-station && pre-commit run --all-files'
scripts/ws.sh 'cd weather-station && scripts/build-gate.sh'   # all of the above + smoke test
```

- Use `-p` after Kconfig, DTS or `.conf` changes.
- Start the container if it isn't running:
  `container run -d --name ws_build --volume "$HOME/Documents/worskpace:/home/zephyr/workspace" ghcr.io/tobiwan88/zephyr_docker:arm sleep infinity`
  then install `pkgconf libsdl2-dev protobuf-compiler mosquitto` (root) and
  `pre-commit gcovr nanopb` (venv). MQTT tests skip without a broker on :1883.
- Do not format with the host `clang-format`: CI pins v17 through pre-commit.

Run the gate after every code, Kconfig or DTS change. A change is done when both apps build,
twister passes and pre-commit passes. Run `/review` before opening a PR. Static analysis:
`scripts/run-codechecker.sh --diff` (compare to `sca/` baseline), `--rebaseline` after fixes.

## Hard rules

These are enforced by `.claude/settings.json` (deny rules and a PostToolUse hook) and by
`scripts/check-standards.sh`:

- No heap: no `malloc`/`free`/`k_malloc`/`k_free`. All memory is static.
- No `strcpy`, `sprintf`, `gets`, `atoi`. No `printk` in `lib/` or `apps/` (use `LOG_*`).
- No `.tflite` models referenced from CMake or Kconfig.
- Never `git add .`/`-A`. Add files by name. Never force-push.

Firmware conventions (loaded automatically when editing `lib/` or `apps/`) are in
`.claude/rules/firmware.md`. Networking and native_sim socket gotchas are in
`.claude/rules/native-sim-net.md`.

## Layout

- `lib/<name>/`: self-contained libraries, each gated by a Kconfig symbol and wired with
  `SYS_INIT`. Apps compose them through `prj.conf` only.
- `apps/`: `gateway`, `sensor-node` (native_sim), `outdoor_sensor_node`, `lora_bridge`.
- `tests/`: ztest suites per library, and `tests/integration/` (pytest against the gateway).
- zbus channels: `sensor_trigger_chan` → sensors → `sensor_event_chan` → consumers.
  `config_cmd_chan` carries runtime config. `remote_*` and `lora_*` channels belong to the
  remote-sensor and LoRa stacks.
- Sensor identity is `sensor_uid`: fake sensors 0x0001–0x00FF, remote nodes 0x0Fxxxxxx,
  broadcast 0xFFFFFFFF. Hardware drivers take the UID from the DT `sensor-uid` property (Kconfig
  fallbacks BME680 0x0011, SEN0460 0x0021). Look up metadata through `sensor_registry`.
  `fota_confirm` and MCUboot only matter on hardware builds.
- `docs/decisions.md`: design decisions that still constrain the code (`ADR-0NN` in comments). `docs/backlog.md`: open work.

## Git

Branch from `master` with a kebab-case name. Commits use `type(scope): summary` (≤ 72
chars; types `feat fix refactor test docs chore ci`), checked by commitizen in pre-commit.
Open PRs against `master`.
