# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SNTP_SYNC)

  zephyr_library()
  zephyr_library_sources(src/sntp_sync.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
