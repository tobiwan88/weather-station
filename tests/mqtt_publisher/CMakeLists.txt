# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_mqtt_publisher)

target_sources(app PRIVATE src/main.c)

# Expose the internal format header so tests can call helpers directly.
target_include_directories(
  app PRIVATE ${CMAKE_SOURCE_DIR}/../../lib/mqtt_publisher/src)
