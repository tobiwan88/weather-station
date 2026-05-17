# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SENSOR_NODE_TX)

  zephyr_library()
  zephyr_library_sources(src/sensor_node_tx.c)

  zephyr_library_include_directories(include)
  zephyr_library_include_directories(
    ${ZEPHYR_CURRENT_MODULE_DIR}/lib/lora_node/include)

endif()
