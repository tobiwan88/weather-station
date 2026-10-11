# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_sensor_node_tx)

# lib/sensor_node_tx requires LORA_NODE in Kconfig, which this test does not
# enable. Compile the library source directly, with its Kconfig values supplied
# as definitions. lora_node_init() and lora_node_transmit() are mocked in
# src/main.c.
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

target_sources(app PRIVATE src/main.c
                           ${LIB_DIR}/sensor_node_tx/src/sensor_node_tx.c)

target_include_directories(
  app PRIVATE ${LIB_DIR}/sensor_node_tx/include ${LIB_DIR}/lora_node/include
              ${LIB_DIR}/sensor_event/include ${LIB_DIR}/config_cmd/include)

zephyr_compile_definitions(
  CONFIG_SENSOR_NODE_TX_MAX_READINGS=20
  CONFIG_SENSOR_NODE_TX_READINGS_PER_PACKET=5
  CONFIG_SENSOR_NODE_TX_INTERVAL_S=3600 CONFIG_SENSOR_NODE_TX_LOG_LEVEL=3)
