# Composition Model

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
priority 80  sntp_sync       — must complete initial NTP query before others read time
priority 90  fake_sensors    — sensor drivers register themselves in the registry
priority 95  gateway         — subscribes to sensor_event_chan for logging
priority 97  http_dashboard  — subscribes to sensor_event_chan, starts HTTP server
priority 99  sensors timer   — starts periodic broadcast (all listeners now registered)
priority 99  clock_display   — schedules 60-second wall-clock tick
             main()          — runs LVGL loop or sleeps forever
```

The ordering matters: the sensors timer fires at priority 99 because all subscribers (gateway at 95, dashboard at 97) must be registered before the first broadcast. A trigger fired before a listener is registered loses the event permanently — zbus has no replay.

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

## The 50-Line `main.c` Rule

`main.c` for the gateway application is kept under 50 lines. It contains:

- `LOG_MODULE_REGISTER` — names the log module
- One `SYS_INIT` call registering the gateway's zbus listener (the one that logs every sensor event)
- `main()` itself: either `lvgl_display_run()` (which never returns) or `k_sleep(K_FOREVER)`

If something cannot be expressed in that budget, it belongs in a library with its own `SYS_INIT`. This rule forces a clean separation: application policy lives in `prj.conf`; mechanism lives in libraries.
