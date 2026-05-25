---
name: new-lib
description: Use when adding a new library under lib/. Creates the full scaffold with Kconfig, CMakeLists.txt, header, source, and wires it into the build system. Invoke when the user says "add library", "new lib", "create library", or similar.
argument-hint: <lib_name> "<description>" <kconfig_symbol> <sys_init_priority>
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Add a New Consumer or Utility Library

Use this when adding a **new library** under `lib/` — e.g. an MQTT publisher,
telemetry codec, data logger, or any other consumer/utility that subscribes to
zbus channels.

**Arguments received**: lib_name=`$0` · description=`$1` · kconfig_symbol=`$2` · sys_init_priority=`$3`

---

## Step 0 — Branch

```bash
git checkout master && git pull
git checkout -b feat/<lib_name>
```

---

## Step 1 — Decide zbus channel ownership

Before writing any code, answer these questions:

1. **Does this library define a new zbus channel?**
   - If yes: `ZBUS_CHAN_DEFINE(...)` goes in exactly one `.c` file in this library.
     `ZBUS_CHAN_DECLARE(...)` goes in the library's public header.
   - If no: the library subscribes to an existing channel via `ZBUS_CHAN_DECLARE` +
     `zbus_chan_add_obs()` in `SYS_INIT`.

2. **Which existing channels does this library consume?**
   - `sensor_event_chan` — carries `env_sensor_data` events
   - `sensor_trigger_chan` — carries `sensor_trigger_event` (fire to trigger all sensors)
   - Other channels: check `lib/*/include/*/` headers

**Architecture rule**: `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel.
Never define a channel that is already defined elsewhere.

---

## Step 2 — Choose SYS_INIT priority

SYS_INIT priority map (APPLICATION level):

| Priority | Module |
|---|---|
| 80 | `sntp_sync` |
| 90 | `fake_temperature` |
| 91 | `fake_humidity`, `lvgl_display` |
| 95 | `gateway` main |
| 99 | `clock_display` auto-timer, `fake_temp` auto-timer |

Rules:
- Consumers of `sensor_event_chan` must initialise **after** their producers (≥92).
- If your library publishes to `sensor_trigger_chan`, stay below 90.
- Pick the lowest available slot that satisfies the ordering constraint.

---

## Step 3 — Create the directory structure

```
lib/<lib_name>/
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── <lib_name>/
│       └── <lib_name>.h
└── src/
    └── <lib_name>.c
```

---

## Step 4 — Write the files

See reference templates in `references/`:
- **Kconfig**: See [`references/scaffold-kconfig`](references/scaffold-kconfig)
- **CMakeLists.txt**: See [`references/scaffold-cmake`](references/scaffold-cmake)
- **Header**: See [`references/scaffold-header`](references/scaffold-header)
- **Source**: See [`references/scaffold-source`](references/scaffold-source)

Replace all `<lib_name>`, `<LIB_NAME>`, `<KCONFIG_SYMBOL>`, `<sys_init_priority>`, and `<description>` placeholders with your actual values.

If your library owns a new zbus channel, uncomment `ZBUS_CHAN_DECLARE` in the header and `ZBUS_CHAN_DEFINE` in the source. If it only subscribes, use `zbus_chan_add_obs()` as shown in the source template.

If your library needs a dedicated thread (rare — prefer zbus listener callbacks), add `K_THREAD_DEFINE` and a `k_thread_entry` function. Document why a thread is needed instead of a listener.

---

## Step 5 — Register the library in the build system

Add to `lib/Kconfig` (at the end of the existing `rsource` list):
```kconfig
rsource "<lib_name>/Kconfig"
```

Add to `CMakeLists.txt` (project root, at the end of the existing `add_subdirectory_ifdef` list):
```cmake
add_subdirectory_ifdef(CONFIG_<KCONFIG_SYMBOL> lib/<lib_name>)
```

---

## Step 6 — Enable the library in app configs

For the gateway app: `apps/gateway/prj.conf`
```
CONFIG_<KCONFIG_SYMBOL>=y
```

For sensor-node: `apps/sensor-node/prj.conf`
```
CONFIG_<KCONFIG_SYMBOL>=y
```

Only enable in the apps that need it.

---

## Step 7 — Verify the build

Kconfig changed → pristine rebuild required:

```bash
west build -p always -b native_sim/native/64 apps/gateway
west build -p always -b native_sim/native/64 apps/sensor-node
```

**Validate:** Read the build output. Confirm "BUILD SUCCESS" and no unresolved symbol errors for your library.

Shell smoke-test:

```bash
printf "help\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Confirm the binary boots without panics.

Then proceed to `/build-and-test` for the full gate.

---

## Step 8 — Commit

```bash
git add lib/<lib_name>/ lib/Kconfig CMakeLists.txt apps/gateway/prj.conf
git commit -m "feat(<lib_name>): add <description>

New library lib/<lib_name>. Kconfig symbol CONFIG_<KCONFIG_SYMBOL>.
Subscribes to sensor_event_chan via zbus listener at SYS_INIT priority <sys_init_priority>.
Enabled in apps/gateway/prj.conf."
```

---

## Red Flags — STOP and re-check

| Feeling | Reality |
|---------|---------|
| "I can use `target_link_libraries()` in the app" | Never. ADR-008: Kconfig-only composition. |
| "This channel is already defined somewhere else" | Don't define it again. `ZBUS_CHAN_DEFINE` in exactly one `.c`. |
| "I need a sensor_manager to poll sensors" | ADR-004: trigger-driven, not polled. Subscribe to channels. |
| "I'll put logic in `main.c`" | ADR-008: main.c = `LOG_MODULE_REGISTER` + `return 0` only. |
| "I'll hardcode sensor_uid in the consumer" | ADR-006: `sensor_uid` is the identity key. Use `sensor_registry`. |

## Next steps

After completing this skill:
- Invoke `/build-and-test` to run the full verification gate
- If the library introduces a new pattern or channel → invoke `/adr` to document the decision
