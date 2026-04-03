# ADR-001 — Repository and West Workspace Structure

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Zephyr projects can be organised in several topologies. The choice determines
how third-party modules are fetched, how the project registers its own drivers
and boards, and how a CI pipeline or new developer initialises a workspace from
scratch. Getting this wrong early forces painful refactoring once the project
grows.

Key constraints:
- The project must work as both a **west manifest** (the entry point for
  `west init`) and a **Zephyr module** (so its drivers, boards, and Kconfig
  are automatically discovered by the build system).
- It must stay lean — only fetch the Zephyr modules it actually needs.
- It must be immediately approachable for open-source contributors who have
  never used Zephyr before.
- It must be the model on which an AI coding agent generates new code, so the
  structure must be deterministic and unambiguous.

---

## Decision

Use Zephyr **T2 topology** (application-as-manifest): the `weather-station`
repo is simultaneously the west manifest repository *and* a Zephyr module.

```
west init -l weather-station/    ← -l means "local manifest"
west update
```

The repo registers itself as a Zephyr module via `zephyr/module.yml`, which
tells the build system where to find custom boards, devicetree bindings, drivers,
and Kconfig. All application logic lives inside the repo — Zephyr and its
dependencies are fetched externally by west and never committed.

### Directory layout

```
weather-station/               ← git repo root, also west manifest
│
├── west.yml                   ← declares Zephyr version + module allowlist
├── zephyr/module.yml          ← registers repo as a Zephyr module
├── CMakeLists.txt             ← module-level: add_subdirectory lib drivers
├── Kconfig                    ← module-level: rsource sub-Kconfigs
├── VERSION                    ← semantic version (MAJOR.MINOR.PATCHLEVEL)
│
├── apps/                      ← one sub-directory per firmware image
│   ├── gateway/               ← Wi-Fi hub + LVGL display
│   └── sensor-node/           ← LoRa TX beacon
│
├── lib/                       ← shared reusable libraries (west modules)
│   ├── sensor_event/          ← env_sensor_data struct, Q31 helpers, zbus channel
│   ├── sensor_trigger/        ← sensor_trigger_event struct, zbus channel
│   ├── sensor_registry/       ← uid → label/location/scaling metadata
│   ├── fake_sensors/          ← DT-instantiated fake drivers + auto-publish timer
│   ├── sntp_sync/             ← SNTP time sync with runtime resync
│   ├── clock_display/         ← wall-clock widget for LVGL display
│   ├── lvgl_display/          ← LVGL display manager (sensor tiles)
│   └── http_dashboard/        ← Chart.js timeseries + config REST API
│                                 (lora_radio, connectivity: future)
│
├── include/common/            ← shared headers (zbus channel declarations,
│                                 data structs, Q31 helpers)
│
├── drivers/                   ← out-of-tree Zephyr drivers (future real HW)
├── dts/bindings/              ← custom devicetree bindings (fake,temperature…)
├── boards/                    ← custom board definitions (future)
│
├── tests/                     ← twister test suites
├── simulation/                ← Renode .resc and Robot Framework scripts (future)
│
├── .devcontainer/             ← VS Code devcontainer (tobiwan88/zephyr_docker)
└── .github/workflows/         ← CI (build + twister + Renode)
```

### West manifest strategy

The `west.yml` uses a `name-allowlist` import to fetch only the Zephyr modules
this project needs, keeping the workspace lean:

```
┌─────────────────────────────────────────────────────┐
│                  west workspace                     │
│                                                     │
│  weather-station/   ← your code (manifest + module) │
│  zephyr/            ← fetched by west               │
│  modules/           ← fetched by west (allowlist)   │
│    hal/nordic/                                      │
│    hal/espressif/                                   │
│    lvgl/                                            │
│    loramac-node/                                    │
│    mbedtls/                                         │
│    …                                                │
└─────────────────────────────────────────────────────┘
```

Without `name-allowlist`, west would clone every Zephyr module (~30+),
most of which this project never uses.

### How apps reference libraries

Apps never reference `lib/` via CMake paths. Instead:

1. `zephyr/module.yml` tells Zephyr the repo root is a module.
2. The root `CMakeLists.txt` calls `add_subdirectory(lib)`.
3. Each `lib/*/CMakeLists.txt` calls `zephyr_library()` (conditional on Kconfig).
4. Apps enable libraries via `prj.conf` Kconfig symbols only.

```
apps/gateway/prj.conf:
  CONFIG_FAKE_SENSORS=y   ← pulls in lib/fake_sensors/ automatically
  CONFIG_LORA_RADIO=y     ← pulls in lib/lora_radio/ automatically
```

No `target_link_libraries()` in app `CMakeLists.txt`. No manual include paths.

---

## Consequences

**Easier:**
- `west init -l .` + `west update` is the complete setup — one command from any
  CI runner or new developer machine.
- Adding a new library = add a directory under `lib/`, write `Kconfig` +
  `CMakeLists.txt`, done. Apps opt in via `prj.conf`.
- Custom boards and DT bindings are auto-discovered — no CMake wiring needed.

**Harder:**
- Contributors unfamiliar with west need to understand the T2 topology before
  they can reason about where files live.
- `ZEPHYR_BASE` must be set for IDE tooling (handled by devcontainer).

**Constrained:**
- Zephyr version is pinned in `west.yml`. Upgrading requires testing all apps
  and updating the `name-allowlist` if new modules are needed.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| T1 (Zephyr as manifest) | Project code would live as a module *under* Zephyr — confusing ownership, harder for contributors to find the entry point |
| T3 (separate manifest repo) | Extra repo to maintain; adds friction for a project that is itself open-source |
| Monorepo with Zephyr vendored | Unacceptably large repo; diverges from upstream Zephyr making security patches painful |
| nRF Connect SDK as base | NCS adds Nordic-specific layers not needed here; locks to Nordic hardware even for the ESP32 gateway target |
