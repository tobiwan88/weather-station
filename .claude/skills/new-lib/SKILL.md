---
name: new-lib
description: Scaffold a new Zephyr library under lib/ with Kconfig, CMakeLists.txt, public header, and source file. Accepts four arguments.
argument-hint: <lib_name> "<description>" <kconfig_symbol> <sys_init_priority>
disable-model-invocation: true
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

## Step 4 — Write `lib/<lib_name>/Kconfig`

```kconfig
# SPDX-License-Identifier: Apache-2.0

menuconfig <KCONFIG_SYMBOL>
	bool "<description>"
	help
	  <One paragraph describing what this library does and when to enable it.>

if <KCONFIG_SYMBOL>

# Add sub-options here, e.g.:
# config <KCONFIG_SYMBOL>_THREAD_STACK_SIZE
# 	int "Thread stack size in bytes"
# 	default 1024

endif # <KCONFIG_SYMBOL>
```

---

## Step 5 — Write `lib/<lib_name>/CMakeLists.txt`

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_<KCONFIG_SYMBOL>)

  zephyr_library()
  zephyr_library_sources(src/<lib_name>.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
```

Do **not** use `target_link_libraries()`. Zephyr's `zephyr_library_*` macros
handle all linking. Apps enable this library via Kconfig only.

---

## Step 6 — Write the public header `lib/<lib_name>/include/<lib_name>/<lib_name>.h`

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef <LIB_NAME>_<LIB_NAME>_H_
#define <LIB_NAME>_<LIB_NAME>_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* If this library owns a new zbus channel, declare it here: */
/* ZBUS_CHAN_DECLARE(<lib_name>_chan); */

/* Public API (if any) */

#ifdef __cplusplus
}
#endif

#endif /* <LIB_NAME>_<LIB_NAME>_H_ */
```

---

## Step 7 — Write `lib/<lib_name>/src/<lib_name>.c`

Canonical structure:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>   /* if consuming sensor events */
#include <<lib_name>/<lib_name>.h>

LOG_MODULE_REGISTER(<lib_name>, LOG_LEVEL_INF);

/* If this library defines a new channel: */
/* ZBUS_CHAN_DEFINE(<lib_name>_chan, <msg_type>, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
 *                 ZBUS_MSG_INIT(0)); */

/* zbus listener callback */
static void <lib_name>_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
	/* process evt */
	ARG_UNUSED(evt);
}

ZBUS_LISTENER_DEFINE(<lib_name>_listener, <lib_name>_cb);

static int <lib_name>_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &<lib_name>_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to add observer: %d", rc);
		return rc;
	}
	LOG_INF("<lib_name>: init done");
	return 0;
}

SYS_INIT(<lib_name>_init, APPLICATION, <sys_init_priority>);
```

---

## Step 8 — Register the library in `lib/Kconfig`

File: `lib/Kconfig`

Add one line **at the end** of the existing `rsource` list:
```kconfig
rsource "<lib_name>/Kconfig"
```

---

## Step 9 — Register the library in root `CMakeLists.txt`

File: `CMakeLists.txt` (project root)

Add one line **at the end** of the existing `add_subdirectory_ifdef` list:
```cmake
add_subdirectory_ifdef(CONFIG_<KCONFIG_SYMBOL> lib/<lib_name>)
```

---

## Step 10 — Enable the library in the relevant app `.conf` file

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

## Step 11 — Run the build gate

```bash
# Kconfig changed → pristine rebuild required
west build -p always -b native_sim/native/64 apps/gateway
west build -p always -b native_sim/native/64 apps/sensor-node

# Shell smoke-test
printf "help\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1

west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
pre-commit run --all-files
```

---

## Step 12 — Commit

```bash
git add lib/<lib_name>/ lib/Kconfig CMakeLists.txt apps/gateway/prj.conf
git commit -m "feat(<lib_name>): add <description>

New library lib/<lib_name>. Kconfig symbol CONFIG_<KCONFIG_SYMBOL>.
Subscribes to sensor_event_chan via zbus listener at SYS_INIT priority <sys_init_priority>.
Enabled in apps/gateway/prj.conf."
```

---

## Common mistakes to avoid

- **Do not** add `target_link_libraries()` in any app `CMakeLists.txt` — use Kconfig only
- **Do not** call `ZBUS_CHAN_DEFINE` for a channel already defined elsewhere
  (`sensor_event_chan` is in `lib/sensor_event/src/sensor_event.c`,
   `sensor_trigger_chan` is in `lib/sensor_trigger/src/sensor_trigger.c`)
- **Do not** create a `sensor_manager` — sensors are trigger-driven, not polled
- **Do not** put application logic in `main.c` — `main.c` should contain only
  `LOG_MODULE_REGISTER` + optional `SYS_INIT` + `k_sleep(K_FOREVER)`
- **Do not** hardcode `sensor_uid` values in consumers — look them up via `sensor_registry`
