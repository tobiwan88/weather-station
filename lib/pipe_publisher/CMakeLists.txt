# SPDX-License-Identifier: Apache-2.0
if(CONFIG_PIPE_PUBLISHER)
  zephyr_library()
  zephyr_library_sources(src/pipe_publisher.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
