# SPDX-License-Identifier: Apache-2.0

if(CONFIG_UART_LORA_BRIDGE)
  zephyr_library()
  zephyr_library_sources(src/uart_lora_bridge.c)
  zephyr_library_include_directories(include)
endif()
