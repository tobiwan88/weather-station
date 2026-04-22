# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MQTT_PUBLISHER)
  zephyr_library()
  zephyr_library_sources(src/mqtt_publisher.c src/mqtt_publisher_format.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

  if(CONFIG_CONFIG_CMD)
    zephyr_library_sources(src/mqtt_publisher_config_cmd.c)
  endif()
endif()
