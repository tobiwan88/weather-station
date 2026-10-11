---
name: new-lib
description: Use when adding a new library under lib/ (a consumer or utility that uses zbus channels). Scaffolds Kconfig, CMakeLists.txt, header, source and build wiring. Triggers: "add library", "new lib", "create library".
argument-hint: <lib_name> "<description>" <KCONFIG_SYMBOL> <sys_init_priority>
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Add a New Library

Creates a self-contained library under `lib/<lib_name>/`. `scripts/scaffold-lib.sh`
embeds every file template, so do not write the scaffold files by hand.

**Arguments**: lib_name=`$0` · description=`$1` · kconfig_symbol=`$2` · sys_init_priority=`$3`

## Steps

1. **Branch**: `git checkout master && git pull && git checkout -b feat/<lib_name>`
2. **Decide the zbus role**:
   - New channel: `ZBUS_CHAN_DEFINE` in exactly one `.c`, `ZBUS_CHAN_DECLARE` in the public header.
   - Existing channel: `ZBUS_CHAN_DECLARE` plus `zbus_chan_add_obs()`. Never redefine a channel.
3. **Pick the SYS_INIT priority**: consumers of `sensor_event_chan` go at 92 or higher, after
   their producers. Publishers to `sensor_trigger_chan` stay below 90. Check for collisions
   with `grep -rn "SYS_INIT" lib/`.
4. **Dry run, then scaffold**:
   ```bash
   scripts/scaffold-lib.sh <lib_name> "<description>" <KCONFIG_SYMBOL> <priority> --dry-run
   scripts/scaffold-lib.sh <lib_name> "<description>" <KCONFIG_SYMBOL> <priority>
   ```
   Options: `--dry-run`, `--skip-build`, `--quiet`. JSON goes to stdout and progress to stderr.
   Exit 0 on success, 1 on error (missing args, library exists, build failure).
   The script creates `lib/<name>/{Kconfig,CMakeLists.txt,include/<name>/<name>.h,src/<name>.c}`,
   registers the library in `lib/Kconfig` and the root `CMakeLists.txt`, and sets
   `CONFIG_<SYMBOL>=y` in `apps/gateway/prj.conf`.
5. **Implement `src/<name>.c`**:
   - Subscribe to channels. Do not poll.
   - Listeners run in the publisher's context (possibly an ISR or timer). They must not block,
     sleep or take mutexes. Hand heavy work to a work item or subscriber thread.
   - Look up sensors through `sensor_registry` by `sensor_uid`. Never hardcode UIDs.
   - Bus access uses `_dt()` variants only (`i2c_write_read_dt`, `spi_transceive_dt`, `gpio_dt_spec`).
   - Return negative errno. Log with `LOG_MODULE_REGISTER` and `LOG_INF`/`LOG_WRN`/`LOG_ERR`.
   - No heap, no `printk` in `lib/`, no `strcpy`/`sprintf`/`gets`/`atoi`.
6. **Compose through Kconfig only**: `apps/*/src/main.c` stays `LOG_MODULE_REGISTER` plus
   `return 0`. Never add `target_link_libraries()` to an app CMake file.
7. **Unit tests**: if the library has logic, add `tests/<lib_name>/` with `testcase.yaml`
   and `src/`, following an existing suite such as `tests/sensor_event/`.

## Verification

Kconfig changed, so build pristine (`-p always`) from the workspace via `scripts/ws.sh`:

```bash
scripts/ws.sh 'west build -p always -b native_sim/native/64 -d build/native_sim_native_64/gateway weather-station/apps/gateway'
scripts/ws.sh 'west build -p always -b native_sim/native/64 -d build/native_sim_native_64/sensor-node weather-station/apps/sensor-node'
scripts/ws.sh 'printf "help\n" | timeout 10 build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout'
scripts/ws.sh 'west twister -p native_sim/native/64 -T weather-station/tests/<lib_name> --inline-logs -v -N'
```

If the library has no suite yet, run `-T weather-station/tests/` (the full suite) instead.
Then run `/build-and-test` for the full gate and `pre-commit run --all-files` before committing.
Commit as `feat(<lib_name>): <summary>` and keep the message at 72 characters or fewer.
