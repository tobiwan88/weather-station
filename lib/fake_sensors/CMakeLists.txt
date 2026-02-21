# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FAKE_SENSORS)

  zephyr_library()
  zephyr_library_sources(src/fake_temperature.c src/fake_humidity.c)
  zephyr_library_sources_ifdef(CONFIG_FAKE_SENSORS_SHELL
                               src/fake_sensors_shell.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

  # Provide the linker section for fake_sensor_entry so the linker script
  # generates _fake_sensor_entry_list_start/end used by STRUCT_SECTION_FOREACH.
  zephyr_linker_sources(SECTIONS fake_sensors_iterables.ld)

endif()
