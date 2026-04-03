# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CONFIG_CMD)

  zephyr_library()
  zephyr_library_sources(src/config_cmd.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
