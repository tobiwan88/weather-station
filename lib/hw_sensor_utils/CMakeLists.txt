# SPDX-License-Identifier: Apache-2.0

if(CONFIG_HW_SENSOR_UTILS)

  zephyr_library()
  zephyr_library_sources(src/hw_sensor_publish.c)

  zephyr_library_include_directories(include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/sensor_event/include)

endif()
