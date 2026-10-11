# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LORA_NODE)

  zephyr_library()
  zephyr_library_sources(src/lora_node.c)

  zephyr_library_sources_ifdef(CONFIG_LORA_NODE_SHELL src/lora_node_shell.c)

  zephyr_library_include_directories(include)

endif()
