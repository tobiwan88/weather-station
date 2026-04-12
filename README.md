# weather-station

A vibe-coded IoT weather station built on [Zephyr RTOS v4.3.0](https://zephyrproject.org/).

---

## Why this project exists

- **Intentionally AI-assisted** — exploring how far vibe-coding can take an embedded project
- **Learning playground** for Zephyr RTOS patterns: zbus, devicetree, Twister, west workspaces
- **Goal:** a working LoRa weather station without writing boilerplate by hand
- No hardware required in v1 — everything runs on `native_sim`

---

## Design philosophy

- **Trigger-driven, no polling** — sensors fire on demand, not on a timer ([ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md))
- **zbus as communication backbone** — decoupled publishers and subscribers ([ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md))
- **Flat 20-byte messages** — LoRa-friendly, no pointers, struct size enforced ([ADR-003](docs/adr/ADR-003-sensor-event-data-model.md))
- **native_sim first** — fast iteration, no hardware dependency in v1 ([ADR-009](docs/adr/ADR-009-native-sim-first.md))
- **Kconfig-driven composition** — apps declare features, libraries provide them ([ADR-008](docs/adr/ADR-008-kconfig-app-composition.md))

Full design rationale: [`docs/adr/`](docs/adr/README.md)

---

## Getting started

**Prerequisites:** Docker or Podman. VS Code + Dev Containers extension is optional but recommended.

```bash
# 1. Open in devcontainer (VS Code: "Reopen in Container")
#    onCreateCommand runs west init + west update automatically

# 2. Build and run
west build -b native_sim/native/64 apps/gateway
west build -t run
```

For all build commands, architecture rules, and Kconfig options: see [`CLAUDE.md`](CLAUDE.md).

---

## Interactive shell

Both `gateway` and `sensor-node` expose a Zephyr shell on UART0.
When you start the binary it prints the pseudoterminal it is listening on:

```text
uart connected to pseudotty: /dev/pts/2
```

Connect from a second terminal:

```bash
screen /dev/pts/2
# or
minicom -D /dev/pts/2
```

At the `uart:~$` prompt:

```text
help                                   # list all commands
fake_sensors list                      # show all sensors from the DT overlay
fake_sensors set <uid> temp 25000      # set value in milli-°C
fake_sensors trigger <uid>             # fire a one-shot sample
kernel uptime                          # simulated uptime in ms
```

Press `Ctrl-A K` (screen) or `Ctrl-A X` (minicom) to disconnect without stopping the simulation.

For scripted use (CI, pipe), pass `-uart_stdinout` to redirect the shell to stdin/stdout:

```bash
printf "help\nfake_sensors list\n" | timeout 8 \
  /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout 2>&1
```

---

## Graphical display (SDL window)

When CONFIG_LVGL_DISPLAY=y is enabled, the app opens a 320×240 SDL window. Because Docker on macOS cannot easily access the host GPU, we use a virtual framebuffer (Xvfb) viewed via VNC.

### macOS Setup (VNC Method)

1. Ensure `devcontainer.json` has `-p 127.0.0.1:5900:5900` in `runArgs` and rebuild the container.
2. Open the macOS Screen Sharing app and connect to `127.0.0.1:5900`.
3. Authenticate with password `zephyr`.

### Troubleshooting: Connection Hangs / "Failed to create screen"

If Screen Sharing hangs on "Connecting..." or logs show GLX errors, reset the display services:

```bash
# 1. Kill stuck display processes
sudo pkill -9 Xvfb; sudo pkill -9 x11vnc

# 2. Manually restart the display stack
./.devcontainer/start-display.sh

# 3. Ensure your shell sees the virtual display
export DISPLAY=:1

# 4. Run the simulation
./build/native_sim/native/64/gateway/zephyr/zephyr.exe
```

### Shell access via VNC session

The simulation exposes a Zephyr shell on a pseudoterminal. Look for this line in the logs:

```text
uart connected to pseudotty: /dev/pts/X
```

Connect from a second terminal:

```bash
screen /dev/pts/X  # Replace X with the number from logs
```

## Web dashboard

When `CONFIG_HTTP_DASHBOARD=y` is set (enabled by default in the gateway), the app
serves a browser-accessible dashboard on port **8080**.

| URL | Description |
|-----|-------------|
| `http://localhost:8080/` | Live Chart.js timeseries for temperature and humidity |
| `http://localhost:8080/config` | Configuration page (trigger interval, SNTP resync) |
| `http://localhost:8080/api/data` | JSON sensor history (ring buffer) |
| `http://localhost:8080/api/config` | JSON current runtime config (GET) / update config (POST) |

Start the gateway binary, then open `http://localhost:8080` in a browser.
The dashboard auto-refreshes by polling `/api/data`.

To change the sampling interval at runtime:

```bash
curl -X POST http://localhost:8080/api/config -d "trigger_interval_ms=2000"
```

To trigger an immediate SNTP resync:

```bash
curl -X POST http://localhost:8080/api/config -d "action=sntp_resync"
```

Kconfig options (in `apps/gateway/prj.conf`):

| Symbol | Default | Purpose |
|--------|---------|---------|
| `CONFIG_HTTP_DASHBOARD_PORT` | `8080` | TCP port |
| `CONFIG_HTTP_DASHBOARD_HISTORY_SIZE` | `60` | Samples per sensor per type |
| `CONFIG_HTTP_DASHBOARD_MAX_SENSORS` | `16` | Max sensor × type slots |

---

## Testing

Unit tests use Zephyr's **ztest** framework; integration tests use **pytest**
via the `pytest-twister-harness` plugin. Both run on `native_sim` — no
hardware required.

```bash
# Run everything (unit + integration)
west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N

# Unit tests only
west twister -p native_sim/native/64 -T tests/ --exclude-tag integration --inline-logs -v -N

# Integration tests only
west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N

# Integration tests filtered by marker (smoke, shell, http, mqtt, e2e)
west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N \
  --pytest-args="-m smoke"
```

Integration tests boot the full gateway stack and interact through the UART
shell, the HTTP dashboard (port 8080), and optionally the MQTT broker
(port 1883). MQTT tests are skipped automatically when no broker is running.

See `tests/integration/pytest/harnesses/` for the Page Object harness classes
and `tests/integration/pytest/test_*.py` for the test files.

---

## Project status / roadmap

| Phase | Status |
|-------|--------|
| v1: `native_sim` gateway + sensor-node | done |
| v1.1: HTTP dashboard (Chart.js + config API) | done |
| Phase 2: Renode emulation | planned |
| Phase 3: Real hardware (LoRa + display) | planned |

---

## License

MIT — see [LICENSE](LICENSE).
