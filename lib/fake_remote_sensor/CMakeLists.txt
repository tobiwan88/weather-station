# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FAKE_REMOTE_SENSOR)

  zephyr_library()
  zephyr_library_sources(src/fake_remote_sensor.c)
  zephyr_library_sources_ifdef(CONFIG_FAKE_REMOTE_SENSOR_SHELL
                               src/fake_remote_sensor_shell.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
