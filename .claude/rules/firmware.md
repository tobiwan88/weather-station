---
paths:
  - "lib/**"
  - "apps/**"
  - "boards/**"
  - "tests/**/*.c"
---

# Firmware conventions

- Libraries talk to each other only through zbus channels, never by direct calls.
  Each channel is defined with `ZBUS_CHAN_DEFINE` in exactly one `.c` file and declared with
  `ZBUS_CHAN_DECLARE` in its public header.
- zbus listeners run in the publisher's context, which may be an ISR or a k_timer callback.
  They must not block, sleep or take mutexes. Hand heavy work to a work item or subscriber thread.
- Sensors are trigger-driven: they react to `sensor_trigger_chan` and publish one
  `struct env_sensor_data` (Q31 value) per measurement on `sensor_event_chan`. No polling loops.
- `apps/*/src/main.c` stays empty apart from `LOG_MODULE_REGISTER`. Logic lives in libraries,
  started by `SYS_INIT`. Apps select libraries in `prj.conf`. Don't use `target_link_libraries`
  in app CMake.
- Bus access uses the devicetree specs (`i2c_write_read_dt`, `spi_transceive_dt`,
  `gpio_dt_spec`). No hardcoded bus addresses or crypto keys.
- Errors are negative errno values, returned or logged, never silently dropped.
- Logging uses `LOG_MODULE_REGISTER` plus `LOG_INF`/`LOG_WRN`/`LOG_ERR`.
- Fake sensors (UIDs 0x0001–0x00FF) are for native_sim and tests. They must never be enabled
  in a hardware build.
- New library: copy the layout of an existing one (`Kconfig`, `CMakeLists.txt`,
  `include/<name>/<name>.h`, `src/`), then add it to `lib/Kconfig` and `lib/CMakeLists.txt`.
  `/new-lib` scaffolds this.
