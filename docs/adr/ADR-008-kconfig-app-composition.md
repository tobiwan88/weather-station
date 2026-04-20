# ADR-008 — Kconfig-Only App Composition

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Zephyr applications can be structured in many ways. Libraries can be linked
explicitly via `target_link_libraries()` in CMake, selected via Kconfig, or
discovered automatically. The choice has a direct impact on:

- How easy it is to port an app to a new board
- How easy it is for an AI agent to generate new features without creating
  CMake coupling mistakes
- Whether swapping a sensor driver (BME280 → SHT4x) requires source changes

The project goal is that **each app's `src/main.c` is under 50 lines** and
each app's `prj.conf` is the **single file that defines what the app does**.
A developer — or AI agent — reading `prj.conf` should be able to understand
the full feature set of the firmware image without reading CMake files.

---

## Decision

Apps compose features **exclusively via Kconfig**. CMakeLists.txt files for
apps contain only the minimal Zephyr boilerplate. All feature selection,
library inclusion, and driver activation is done in `prj.conf` and board
`.conf` files.

### App CMakeLists.txt template (complete — nothing else needed)

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gateway)
target_sources(app PRIVATE src/main.c)
```

That is the entire file. No `target_link_libraries()`. No
`add_subdirectory()`. No `include_directories()`.

### How libraries are conditionally linked

Each library in `lib/` uses this CMake pattern:

```cmake
# lib/fake_sensors/CMakeLists.txt
if(CONFIG_FAKE_SENSORS)
  zephyr_library()
  zephyr_library_sources(
    src/fake_temperature.c
    src/fake_humidity.c
    src/fake_subsystem.c
    src/fake_shell.c
  )
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)  # expose to app
endif()
```

When `CONFIG_FAKE_SENSORS=y` in `prj.conf`, CMake automatically links the
library. When `n`, the library's source files are not compiled.

### `prj.conf` as the app's feature manifest

```ini
# apps/gateway/prj.conf
# ─────────────────────────────────────────────────────────────
# Reading this file tells you everything the gateway app does.
# No CMake files needed to understand feature composition.
# ─────────────────────────────────────────────────────────────

# Core Zephyr services
CONFIG_ZBUS=y
CONFIG_ZBUS_CHANNEL_NAME=y
CONFIG_ZBUS_OBSERVER_NAME=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3

# Sensor pipeline
CONFIG_SENSOR_EVENT=y           # lib/sensor_event/
CONFIG_SENSOR_TRIGGER=y         # lib/sensor_trigger/
CONFIG_SENSOR_REGISTRY=y        # lib/sensor_registry/
CONFIG_SENSOR_POLL_INTERVAL_S=30

# Sensor backends — choose one per category per app:
CONFIG_FAKE_SENSORS=y           # lib/fake_sensors/ (native_sim)
# CONFIG_BME280=y               # ← swap to this for real HW
# CONFIG_SHT4X=y                # ← or this

# Radio
CONFIG_LORA=y
CONFIG_LORA_RADIO=y             # lib/lora_radio/

# Connectivity
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_WIFI=y
CONFIG_NET_DHCPV4=y
CONFIG_MQTT_LIB=y
CONFIG_MQTT_KEEPALIVE=60
CONFIG_HTTP_SERVER=y

# Display
CONFIG_DISPLAY=y
CONFIG_LVGL=y
CONFIG_LV_MEM_SIZE=8192
CONFIG_LV_USE_LABEL=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_DISPLAY_MANAGER=y        # lib/display_manager/

# Memory
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_HEAP_MEM_POOL_SIZE=32768
```

### Board-specific `.conf` overlays

Board-specific Kconfig additions live in `apps/gateway/boards/<board>.conf`.
These override or extend `prj.conf` without modifying it:

```ini
# apps/gateway/boards/esp32_devkitc_wroom.conf
# Real hardware: swap fake sensors for BME280
CONFIG_FAKE_SENSORS=n
CONFIG_BME280=y
CONFIG_I2C=y
CONFIG_SPI=y
CONFIG_WIFI_ESP32=y
```

Switching from `native_sim` to `esp32_devkitc_wroom` at build time:
```bash
west build -b esp32_devkitc_wroom/esp32/procpu apps/gateway
```

The board `.conf` is merged automatically by Zephyr's CMake. No source changes.

### Kconfig dependency chain

Kconfig `depends on` statements enforce correct feature ordering:

```kconfig
# lib/fake_sensors/Kconfig
menuconfig FAKE_SENSORS
    bool "Fake sensor drivers"
    depends on ZBUS
    depends on SENSOR_EVENT
    depends on SENSOR_TRIGGER
    depends on SHELL
    help
      Fake sensors for native_sim and testing.
      NEVER enable on production hardware.
```

If `CONFIG_SENSOR_EVENT=n` but `CONFIG_FAKE_SENSORS=y`, Kconfig reports a
dependency error before any C code is compiled. Misconfigured `prj.conf`
files are caught immediately.

### Kconfig defaults

All library Kconfig symbols default to `n`. This means:
- An app that doesn't mention a library never links it.
- Adding a new library to `lib/` never silently affects existing apps.
- CI builds have a known-good baseline: only what `prj.conf` explicitly enables.

```kconfig
# lib/display_manager/Kconfig
menuconfig DISPLAY_MANAGER
    bool "LVGL display manager"
    default n          ← explicit n default
    depends on DISPLAY
    depends on LVGL
    depends on ZBUS
    depends on SENSOR_EVENT
    depends on SENSOR_REGISTRY
```

### `main.c` 50-line rule

The 50-line rule is enforced by design, not tooling. If `main.c` grows beyond
50 lines, the excess logic *probably* belongs in a library. The rule forces the
right question: "Which library owns this?" instead of "How do I make main.c work?"

### When app-level C code is acceptable

The library approach is strongly preferred, but not every line of C must live
in a library. Code may stay in `apps/*/src/` when it satisfies **both**:

1. **Tightly coupled to this application's specific policy or hardware** —
   it encodes decisions (timing strategy, startup sequence, board-specific
   wiring) that are unique to this firmware image and would be rewritten
   from scratch for a different app.
2. **No reuse value** — it would never make sense to enable this via Kconfig
   in another app.

| Code | Where it belongs | Why |
|------|-----------------|-----|
| Startup trigger + sampling timer in `main.c` | App | Gateway policy — a sensor-node app has a completely different timing strategy |
| `SYS_INIT` call that wires two libraries for this specific image | App | The wiring is image-specific, not reusable |
| Display layout logic | Library (`lib/display_manager`) | Another app might use the same display |
| Q31 encode/decode helpers | Library (`lib/sensor_event`) | Needed by every sensor driver |
| LoRa channel config specific to this deployment | App | Deployment-specific, not a reusable abstraction |
| Sensor event console logging | Library (`lib/sensor_event_log`) | Both gateway and sensor-node needed identical logging — duplication proved reuse value |

**Rule of thumb:** if you find yourself wanting to write a Kconfig symbol for
it, it belongs in a library. If it would be nonsensical to reuse it in any
other app, it can stay in `apps/*/src/`.

Target shape for `apps/gateway/src/main.c`:

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station gateway v0.1.0");
	return 0;
}
```

On native_sim the Zephyr scheduler keeps running after `main()` returns; no
`k_sleep(K_FOREVER)` is needed. All subsystems are wired via `SYS_INIT`.

> **Note (backlog):** The current implementation still calls `lvgl_display_run()`
> from `main()` when `CONFIG_LVGL_DISPLAY=y`. This is a known ADR violation tracked
> in `docs/backlog.md` ([ADR-008-RULE4]). Fix: move the LVGL timer loop into a
> private Zephyr thread inside `lib/lvgl_display`.

### ADR-008-REVIEW resolution (2026-04-03)

Both `apps/gateway/src/main.c` (previously 77 lines) and
`apps/sensor-node/src/main.c` (previously 66 lines) exceeded the 50-line
limit because both contained an identical zbus listener that logged
`env_sensor_data` events to the console.

**Decision: extract to `lib/sensor_event_log/`** (enabled via
`CONFIG_SENSOR_EVENT_LOG=y`). Rationale:

- The pattern was *duplicated across two apps* — the strongest possible
  signal of reuse value, which disqualifies it from "app-level C code"
  under criterion 2 of this ADR.
- A `CONFIG_SENSOR_EVENT_LOG` Kconfig symbol is natural: any future app
  that wants console-visible sensor readings enables it in `prj.conf`.
- The library self-registers via `SYS_INIT`; no call needed from `main.c`.

Log format (human-readable, sample-time timestamp):

```
[00:01:23.456]  0x1234  TEMP    23.45 °C
[00:01:23.456]  0x1234  HUM     65.2 %RH
```

---

## Consequences

**Easier:**
- A developer (or AI agent) can understand the entire feature set of a firmware
  image by reading one file: `prj.conf`.
- Porting to new hardware = write a board overlay `.dts` and a board `.conf`.
  Zero source changes.
- CI build matrix is trivially extended: add a new row to the matrix with
  a new board name.
- AI-assisted development: include `prj.conf` in the agent's context, and it
  can correctly generate new library code that follows the same pattern.

**Harder:**
- `prj.conf` can become long for feature-rich apps. Structure it with inline
  comments grouping related symbols (see example above).
- Kconfig `depends on` chains can produce surprising "invisible" effects when
  a dependency is disabled. Use `west build --cmake-only` + `ninja menuconfig`
  to inspect the resolved configuration.

**Constrained:**
- App `CMakeLists.txt` must never contain feature-selection logic. If a
  reviewer sees `if(SOME_CONDITION) target_link_libraries(...)` in an app
  `CMakeLists.txt`, it is a violation of this ADR. This rule is absolute —
  it applies to CMake only, not to C source in `apps/*/src/` (see "When
  app-level C code is acceptable" above).
- Library `CMakeLists.txt` may only gate on `if(CONFIG_...)` — the direct
  Kconfig condition. No custom CMake variables.
- When in doubt, put it in a library. Extracting app-level code into a library
  later is cheap; untangling a bloated app is not.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| `target_link_libraries()` in app CMake | Feature composition split across CMake + Kconfig = two places to understand; CMake is harder to read than Kconfig; AI agents more often get CMake wrong |
| Zephyr modules as separate git repos | Over-engineering for a single-repo open-source project; west multi-repo management adds contributor friction |
| Single monolithic CMakeLists.txt | Does not scale; adding a feature requires editing a central file; no conditional compilation |
| Feature flags via C `#define` in source | Preprocessor flags visible only at C level; Kconfig dependency checking lost; harder to inspect from outside |
