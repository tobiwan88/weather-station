# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_lora_session)

target_sources(app PRIVATE src/main.c)

# Compile lora_session.c so session functions are linkable.
target_sources(
  app PRIVATE ${CMAKE_SOURCE_DIR}/../../lib/lora_radio/src/lora_session.c)

# Expose the lora_radio public header.
target_include_directories(
  app PRIVATE ${CMAKE_SOURCE_DIR}/../../lib/lora_radio/include)

# Define Kconfig constants that would normally come from lora_radio/Kconfig.
target_compile_definitions(app PRIVATE CONFIG_LORA_RADIO_SESSION_MAX=4)
