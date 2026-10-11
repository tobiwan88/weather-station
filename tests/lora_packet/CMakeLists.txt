# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_lora_packet)

target_sources(app PRIVATE src/main.c)

# Compile lora_packet.c so encode/decode functions are linkable.
target_sources(
  app PRIVATE ${CMAKE_SOURCE_DIR}/../../lib/lora_radio/src/lora_packet.c)

# Expose the lora_radio public header for frame/packet tests.
target_include_directories(
  app PRIVATE ${CMAKE_SOURCE_DIR}/../../lib/lora_radio/include)
