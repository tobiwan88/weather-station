# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_lora_packet)

# lib/lora_proto is built by the module (CONFIG_LORA_PROTO=y in prj.conf).
target_sources(app PRIVATE src/main.c)
