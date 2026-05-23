# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SEN0460_SENSOR)

  zephyr_library()
  zephyr_library_sources(src/sen0460_sensor.c)
  zephyr_library_sources(src/sen0460_driver.c)

  zephyr_library_include_directories(include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/sensor_event/include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/sensor_trigger/include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/sensor_registry/include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/hw_sensor_utils/include)

endif()
