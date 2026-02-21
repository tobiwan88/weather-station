# weather-station

A vibe-coded IoT weather station built on [Zephyr RTOS v4.2.0](https://zephyrproject.org/).

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
west build -p always -b native_sim apps/gateway
west build -t run
```

For all build commands, architecture rules, and Kconfig options: see [`CLAUDE.md`](CLAUDE.md).

---

## Project status / roadmap

| Phase | Status |
|-------|--------|
| v1: `native_sim` gateway + sensor-node | done |
| Phase 2: Renode emulation | planned |
| Phase 3: Real hardware (LoRa + display) | planned |

---

## License

MIT — see [LICENSE](LICENSE).
