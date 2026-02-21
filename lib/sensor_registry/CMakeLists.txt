# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SENSOR_REGISTRY)

  zephyr_library()
  zephyr_library_sources(src/sensor_registry.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
