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

## Use the scaffold script

Instead of creating files manually, use the scaffold script:

```bash
scripts/scaffold-lib.sh <lib_name> "<description>" <kconfig_symbol> <sys_init_priority>
```

**Options:**
- `--dry-run` — Show what would be created without writing files
- `--skip-build` — Skip the build verification step
- `--quiet` — Suppress stderr progress output

**Example:**
```bash
scripts/scaffold-lib.sh mqtt_publisher "MQTT telemetry publisher" MQTT_PUBLISHER 95
scripts/scaffold-lib.sh mqtt_publisher "MQTT telemetry publisher" MQTT_PUBLISHER 95 --dry-run
```

**Output:** JSON to stdout with created files list, build status, and overall verdict. Human-readable progress goes to stderr.

**JSON output schema:**
```json
{
  "script": "scaffold-lib",
  "timestamp": "2026-05-25T12:00:00Z",
  "verdict": "PASS|FAIL|DRY_RUN",
  "lib_name": "mqtt_publisher",
  "kconfig_symbol": "CONFIG_MQTT_PUBLISHER",
  "sys_init_priority": 95,
  "files_created": [
    "lib/mqtt_publisher/Kconfig",
    "lib/mqtt_publisher/CMakeLists.txt",
    "lib/mqtt_publisher/include/mqtt_publisher/mqtt_publisher.h",
    "lib/mqtt_publisher/src/mqtt_publisher.c",
    "lib/Kconfig (modified)",
    "CMakeLists.txt (modified)",
    "apps/gateway/prj.conf (modified)"
  ],
  "build_status": "PASS|FAIL|SKIPPED|N/A",
  "build_output": "...",
  "summary": "Created 7 files for lib/mqtt_publisher."
}
```

**Exit codes:** 0 = success, 1 = error (missing args, file exists, build failed)

## Manual steps (for reference)

The script performs these steps:

1. **Branch** — `git checkout master && git pull && git checkout -b feat/<lib_name>`
2. **Decide zbus channel ownership** — Does this library define a new channel? If yes, `ZBUS_CHAN_DEFINE` in exactly one `.c`. If no, subscribe via `ZBUS_CHAN_DECLARE` + `zbus_chan_add_obs()`.
3. **Choose SYS_INIT priority** — Consumers of `sensor_event_chan` must initialise **after** their producers (≥92). Publishers to `sensor_trigger_chan` stay below 90.
4. **Create directory structure** — `lib/<lib_name>/{CMakeLists.txt, Kconfig, include/<lib_name>/<lib_name>.h, src/<lib_name>.c}`
5. **Write files** — See reference templates in `references/`
6. **Register in build system** — Add to `lib/Kconfig` and root `CMakeLists.txt`
7. **Enable in app configs** — `CONFIG_<KCONFIG_SYMBOL>=y` in `apps/gateway/prj.conf`
8. **Verify the build** — Pristine rebuild required (Kconfig changed)

## Architecture rules

- **Architecture rule**: `ZBUS_CHAN_DEFINE` in exactly one `.c` per channel. Never define a channel that is already defined elsewhere.
- **Do not** add `target_link_libraries()` in any app `CMakeLists.txt` — use Kconfig only
- **Do not** create a `sensor_manager` — sensors are trigger-driven, not polled
- **Do not** put application logic in `main.c` — `main.c` should contain only `LOG_MODULE_REGISTER` + optional `SYS_INIT` + `k_sleep(K_FOREVER)`
- **Do not** hardcode `sensor_uid` values in consumers — look them up via `sensor_registry`

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
