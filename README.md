# weather-station

A vibe-coded IoT weather station built on [Zephyr RTOS v4.4.0](https://zephyrproject.org/).

---

## Why this project exists

- **Intentionally AI-assisted** — exploring how far vibe-coding can take an embedded project
- **Learning playground** for Zephyr RTOS patterns: zbus, devicetree, Twister, west workspaces
- **Goal:** a working LoRa weather station without writing boilerplate by hand
- No hardware required in v1 — everything runs on `native_sim`

---

## Design philosophy

- **Trigger-driven, no polling** — sensors fire on demand, not on a timer
- **zbus as communication backbone** — decoupled publishers and subscribers
- **Flat sensor messages** — LoRa-friendly, no pointers, no heap
- **native_sim first** — fast iteration, no hardware dependency in v1
- **Kconfig-driven composition** — apps declare features, libraries self-wire

Design rationale: [`docs/adr/`](docs/adr/README.md) · System architecture: [`docs/architecture/`](docs/architecture/README.md)

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

Both `gateway` and `sensor-node` expose a Zephyr shell on UART0. At startup the binary prints the pseudoterminal it listens on (`uart connected to pseudotty: /dev/pts/2`). Connect with `screen /dev/pts/2` or `minicom -D /dev/pts/2`.

```text
fake_sensors list                      # show sensors from the DT overlay
fake_sensors set <uid> temp 25000      # set value in milli-°C
fake_sensors trigger <uid>             # fire a one-shot sample
kernel uptime                          # simulated uptime in ms
```

For scripted/CI use, pass `-uart_stdinout` to redirect the shell to stdin/stdout.

---

## Graphical display (SDL window)

When `CONFIG_LVGL_DISPLAY=y` is enabled, the app opens a 320×240 SDL window. Docker on macOS uses Xvfb + VNC: ensure `-p 127.0.0.1:5900:5900` is in `devcontainer.json` and connect via macOS Screen Sharing to `127.0.0.1:5900` (password: `zephyr`).

If the display hangs, reset with `./.devcontainer/start-display.sh` and re-export `DISPLAY=:1`.

---

## Web dashboard

When `CONFIG_HTTP_DASHBOARD=y` is set (gateway default), the app serves a dashboard on port **8080**.

| URL | Description |
|-----|-------------|
| `http://localhost:8080/login` | Login page |
| `http://localhost:8080/` | Live Chart.js timeseries |
| `http://localhost:8080/config` | Config page (trigger interval, SNTP resync, sensor metadata, locations) |
| `http://localhost:8080/api/data` | JSON sensor history (ring buffer) |
| `http://localhost:8080/api/config` | JSON config (GET) / update (POST) |

**Auth** (`CONFIG_HTTP_DASHBOARD_AUTH=y`): browser login at `/login` with `admin`/`admin`. For API calls, retrieve the bearer token from the Zephyr shell (`http_dashboard token show`) and pass it as `Authorization: Bearer <token>`.

---

## Testing

```bash
# All tests (unit + integration)
west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N

# Integration tests only
west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N

# Filter by marker (smoke, shell, http, mqtt, e2e, system)
west twister -p native_sim/native/64 -T tests/integration --inline-logs -v -N \
  --pytest-args="-m smoke"
```

Integration tests boot the full gateway and interact through the UART shell, HTTP dashboard (port 8080), and optionally MQTT (auto-skipped when no broker is running). See [`docs/architecture/integration-tests.md`](docs/architecture/integration-tests.md) for harness details.

---

## License

MIT — see [LICENSE](LICENSE).
