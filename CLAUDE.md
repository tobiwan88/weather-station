# CLAUDE.md — weather-station

Project context for AI coding agents. Read this before writing any code.
Full rationale is in [`docs/adr/`](docs/adr/README.md).

---

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.3.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Primary target** | `native_sim/native/64` |
| **Container** | `devcontainer (Dockerfile); Xvfb :1 + x11vnc on port 5900` |
---

## Essential commands

```bash

west init -l .                       # first-time workspace setup
west update --narrow                 # fetch Zephyr + allowlist modules

west build -b native_sim/native/64 apps/gateway              # incremental (own build dir)
west build -b native_sim/native/64 apps/sensor-node
west build -p always -b native_sim/native/64 apps/gateway    # pristine (Kconfig/DTS changes)
west build -t run                    # run the last built app

# Run gateway with graphical SDL window (VNC viewer → localhost:5900, pw: zephyr)
/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe

west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
pre-commit run --all-files
```

---

## Display / VNC

The devcontainer runs an internal Xvfb virtual framebuffer on `:1` and exposes it
via x11vnc on port 5900 (mapped to `127.0.0.1:5900` on the host).

Connect with any VNC viewer:
  - Address : `localhost:5900`
  - Password : `zephyr`
  - macOS tip: open `vnc://localhost:5900` in Finder → Go → Connect to Server

The startup script `.devcontainer/start-display.sh` is called automatically on
container start. If the SDL window never appears or the display hangs, reset:

```bash
pkill -9 Xvfb x11vnc 2>/dev/null || true
bash .devcontainer/start-display.sh
```

All three env vars are pre-set in `devcontainer.json`; no manual export needed.

---

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.**
Temperature and humidity from the same chip are two separate `env_sensor_data`
events on `sensor_event_chan`. Never bundle multiple measurements.

**2. `env_sensor_data` is a flat 20-byte struct — no pointers.**
```c
struct env_sensor_data {
    uint32_t         sensor_uid;    // DT sensor-uid property
    enum sensor_type type;          // what physical quantity
    int32_t          q31_value;     // Q31 fixed-point value
    int64_t          timestamp_ms;  // k_uptime_get()
};                                  // no pointers; fits in a cache line
```

**3. No sensor manager. No polling. No tight coupling.**
Sensors subscribe to `sensor_trigger_chan`. Any publisher fires them.
See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` contains only:** `LOG_MODULE_REGISTER` + optional `SYS_INIT` +
`k_sleep(K_FOREVER)`. All logic lives in libraries.
See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs.
Instantiated via `DT_FOREACH_STATUS_OKAY`. Shell-controllable.
See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

**6. `sensor_uid` is the identity key.** `sensor_registry` maps uid → metadata.
Display/MQTT use the registry — never hardcode UIDs in consumers.

**7. zbus channel ownership is strict.**
`ZBUS_CHAN_DEFINE` in exactly one `.c` per channel.
`ZBUS_CHAN_DECLARE` in the public header only.
See [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md).
trigger_chan → fake_sensor → sensor_event_chan → (consumers)

**8. Apps configure features via Kconfig only.**
No `target_link_libraries()` in app `CMakeLists.txt`.
See [ADR-001](docs/adr/ADR-001-repo-and-workspace-structure.md).

---

## Agent workflow (non-negotiable)

Follow this workflow for **every** task that touches source code.

### 1. Branch first
Before writing any code, create a feature branch from `main`:
```bash
git checkout main && git pull
git checkout -b <short-kebab-description>   # e.g. feat/sensor-uid-logging
```
Never commit directly to `main` or `master`.

### 2. Incremental changes + build gate
Make the smallest possible logical change, then verify it compiles.

Each app/board combination gets its own build directory (`build/{board}/{app}/`)
so you can build both apps without wiping each other:

```bash
# Incremental rebuild — normal case for source-only changes
west build -b native_sim/native/64 apps/gateway
west build -b native_sim/native/64 apps/sensor-node

# Pristine rebuild — only needed after Kconfig or DTS changes
west build -p always -b native_sim/native/64 apps/gateway
```
If the build fails, fix it before touching anything else.

### 3. Shell smoke-test
After a successful build, run the binary and probe it via the shell to
verify observable runtime behaviour before running the full suite.
For a pure log check use `-uart_stdinout` (no display needed).
For the visual test, ensure VNC is connected before launching the binary.
Use `-uart_stdinout` so the shell is available on stdin/stdout for piping:

```bash
# Quick sanity check — pipe commands, capture output
printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Check for:
- `help` lists `fake_sensors` (shell is up, library linked)
- `fake_sensors list` shows the sensors from the DT overlay (DT wiring correct)
- Startup log shows expected sensor init messages and auto-publish events

If the output is wrong, fix it before running Twister.
Run this check after **every** build when touching sensor, zbus, or shell code.

### 4. Test gate (when source code is touched)
Run the full test suite after every non-trivial change:
```bash
west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
```
All tests must be green before committing. Never commit a red suite.

### 5. Commit per logical unit
Once build and tests are green, run pre-commit and commit:
```bash
pre-commit run --all-files
git add <changed files>
git commit -m "<type>(<scope>): <imperative summary>"
```
Commit message conventions:
- **type**: `feat` | `fix` | `refactor` | `test` | `docs` | `chore`
- **scope**: library or app name, e.g. `fake_sensors`, `gateway`
- **summary**: imperative, ≤72 chars, no period
- **body** (optional): add a short body when the change is non-obvious —
  one sentence per key decision or side-effect, enough for an agent to
  reconstruct *what changed and why* if bisecting a regression.
  Keep it factual: file paths, symbol names, config keys changed.
  Example:
  ```
  feat(fake_sensor): publish humidity as separate zbus event

  Split env_sensor_publish() into two calls — one per sensor_type.
  Updated tests/fake_sensor/src/main.c expectations accordingly.
  No Kconfig or DTS changes.
  ```

Repeat steps 2–5 for each logical unit of work.

### 6. Hand off to user for merge
When all work on the branch is done, tell the user:
> "Branch `<name>` is ready. All builds pass and tests are green.
> Run `git merge --no-ff <name>` or open a PR to merge into main."

Do **not** merge, push, or open a PR yourself unless explicitly asked.

---
## What NOT to create

- A `sensor_manager` module of any kind
- Any polling loop calling `sensor_sample_fetch()`
- `CONFIG_BME280`, `CONFIG_SHT4X`, or any real sensor driver
- Wi-Fi, MQTT, or HTTP code (not in scope yet)
- Files under `.west/` or `build/`
