# SPDX-License-Identifier: Apache-2.0

if(CONFIG_REMOTE_SENSOR)

  zephyr_library()
  zephyr_library_sources(
    src/remote_discovery_chan.c src/remote_scan_ctrl_chan.c
    src/remote_sensor_manager.c)
  zephyr_library_sources_ifdef(CONFIG_REMOTE_SENSOR_PERSIST
                               src/remote_sensor_settings.c)
  zephyr_library_sources_ifdef(CONFIG_REMOTE_SENSOR_SHELL
                               src/remote_sensor_shell.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

  # Linker section for remote_transport iterable structs. Generates
  # _remote_transport_list_start/_end used by STRUCT_SECTION_FOREACH.
  zephyr_linker_sources(SECTIONS remote_sensor_iterables.ld)

endif()
