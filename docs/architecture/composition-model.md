# Composition Model

> Design rationale: [ADR-008](../adr/ADR-008-kconfig-app-composition.md).

## The Problem with Explicit Wiring

In a conventional embedded application, `main.c` acts as a compositor:

```c
// conventional approach — main.c knows everything
sensor_init(&temp_sensor);
display_init(&lcd);
mqtt_init(&broker);
while (1) {
    val = sensor_read(&temp_sensor);
    display_update(&lcd, val);
    mqtt_publish(&broker, val);
    k_sleep(K_MSEC(5000));
}
```

Every new feature requires editing `main.c`. The feature set is visible only by reading `main.c`. Removing a feature means finding and deleting its calls throughout the file. This does not scale past a handful of features.

---

## Kconfig-Driven Composition

This project inverts that model. Feature selection happens entirely in `prj.conf`:

```
CONFIG_HTTP_DASHBOARD=y
```

That single line causes the Zephyr build system to:

1. Compile `lib/http_dashboard/` (via `add_subdirectory_ifdef` in the root CMakeLists)
2. Make its `Kconfig` options available and enforce its `depends on` constraints
3. Link it into the firmware

`main.c` does not change. No `#include` for the dashboard is added anywhere in the application. The library wires itself in.

The mechanism is `SYS_INIT`. Each library registers its own initialisation function at a declared priority within the `APPLICATION` level. At boot, Zephyr calls these in priority order before `main()`. The library subscribes itself to the relevant zbus channel, starts its HTTP server, and is fully operational before the application entry point runs.

---

## SYS_INIT as the Composition Point

The initialisation chain is the application's wiring diagram. Reading it tells you every feature that is active and the order they come up:

```
priority 80  sntp_sync              — initial SNTP query before timestamps are needed
priority 90  fake_temperature       — registers in sensor_registry, subscribes trigger chan
priority 91  fake_humidity          — same; lvgl_display also at 91 (subscribes event chan)
priority 92  fake_co2               — same; remote_sensor_manager also at 92
             uart_lora_sender       — co-processor: subscribes sensor_event_chan, sends UART frames
priority 93  fake_voc               — same; fake_remote_sensor also at 93
priority 94  remote_sensor_settings — loads persisted peer list after manager is up
priority 95  sensor_event_log       — gateway listener subscribes to sensor_event_chan
             (+ settings loads)       sensor_registry, sntp_sync, fake_sensors config-cmd callbacks
priority 96  location_registry      — loads persisted location names after registry is up
priority 97  http_dashboard         — subscribes to sensor_event_chan, starts HTTP server
priority 98  mqtt_publisher         — subscribes to sensor_event_chan, connects to MQTT broker
priority 99  fake_sensors_timer     — starts periodic broadcast (all listeners now registered)
priority 99  clock_display          — schedules 60-second wall-clock tick
             main()                 — runs LVGL loop or k_sleep(K_FOREVER)
```

The ordering matters: the sensors timer fires at priority 99 because all subscribers (sensor_event_log at 95, http_dashboard at 97, mqtt_publisher at 98) must be registered before the first broadcast. A trigger fired before a listener is registered loses the event permanently — zbus has no replay.

---

## Why No `target_link_libraries()`

Conventional Zephyr applications link libraries explicitly in CMakeLists:

```cmake
target_link_libraries(app PRIVATE my_library)
```

This project does not do that. Instead, each library uses `zephyr_library()` which makes it a Zephyr module-level library, automatically linked when compiled. Combined with `add_subdirectory_ifdef(CONFIG_FOO lib/foo)` at the root, a library is compiled and linked if and only if its Kconfig symbol is set. The application CMakeLists needs no knowledge of which libraries exist.

The consequence: the feature manifest is `prj.conf`, not `CMakeLists.txt`. A developer enabling or disabling a feature touches exactly one file.

---

## Library Boundaries

Each library owns:
- Its public header under `include/<name>/<name>.h` — the only permitted import surface for other code
- Its Kconfig symbol — other libraries may `depends on` it, but may not call its functions
- Its `SYS_INIT` callback — the only place it registers with the rest of the system

Libraries are not permitted to `#include` each other's headers. They share state only through zbus channels. This is enforced by convention, not the build system — but a violation is immediately visible because it creates a circular dependency in the `depends on` graph.

The two structs in `include/common/weather_messages.h` are the system's shared contract. Everything else is private to its library.

---

## File Templates

### App CMakeLists.txt

The complete contents of an app's `CMakeLists.txt` — nothing else is needed:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gateway)
target_sources(app PRIVATE src/main.c)
```

No `target_link_libraries()`. No `add_subdirectory()`. No `include_directories()`.

### Library CMakeLists.txt pattern

Each library in `lib/` gates its sources on its Kconfig symbol:

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

### Full prj.conf example

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

### Board-specific .conf overlay

Board-specific Kconfig additions live in `apps/gateway/boards/<board>.conf`. These override or extend `prj.conf` without modifying it:

```ini
# apps/gateway/boards/esp32_devkitc_wroom.conf
# Real hardware: swap fake sensors for BME280
CONFIG_FAKE_SENSORS=n
CONFIG_BME280=y
CONFIG_I2C=y
CONFIG_SPI=y
CONFIG_WIFI_ESP32=y
```

### Kconfig dependency chain

`depends on` statements enforce correct feature ordering and catch misconfigured `prj.conf` files before any C code is compiled:

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

All library Kconfig symbols default to `n` — an app that doesn't mention a library never links it.

### When app-level C code is acceptable

Code may stay in `apps/*/src/` when it satisfies **both**:

1. **Tightly coupled to this application's specific policy or hardware** — encodes decisions unique to this firmware image that would be rewritten from scratch for a different app.
2. **No reuse value** — it would never make sense to enable this via Kconfig in another app.

| Code | Where it belongs | Why |
|------|-----------------|-----|
| Startup trigger + sampling timer in `main.c` | App | Gateway policy — a sensor-node app has a completely different timing strategy |
| `SYS_INIT` call that wires two libraries for this specific image | App | The wiring is image-specific, not reusable |
| Display layout logic | Library (`lib/display_manager`) | Another app might use the same display |
| Q31 encode/decode helpers | Library (`lib/sensor_event`) | Needed by every sensor driver |
| LoRa channel config specific to this deployment | App | Deployment-specific, not a reusable abstraction |
| Sensor event console logging | Library (`lib/sensor_event_log`) | Both gateway and sensor-node needed identical logging — duplication proved reuse value |

**Rule of thumb:** if you find yourself wanting to write a Kconfig symbol for it, it belongs in a library.

---

## The 50-Line `main.c` Rule

`main.c` for the gateway application is kept under 50 lines. It contains:

- `LOG_MODULE_REGISTER` — names the log module
- `main()` itself: either `lvgl_display_run()` (which never returns) or `k_sleep(K_FOREVER)`

If something cannot be expressed in that budget, it belongs in a library with its own `SYS_INIT`. This rule forces a clean separation: application policy lives in `prj.conf`; mechanism lives in libraries.

> **Known violation:** `main()` currently calls `lvgl_display_run()` directly. This is tracked in the backlog as `[ADR-008-RULE4]`. The fix is to start the LVGL thread from `lvgl_display_init()` via `SYS_INIT`, making `main.c` a pure `LOG_MODULE_REGISTER + return 0`.
