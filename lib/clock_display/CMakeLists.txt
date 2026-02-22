# SPDX-License-Identifier: Apache-2.0

if(CONFIG_CLOCK_DISPLAY)

  zephyr_library()
  zephyr_library_sources(src/clock_display.c)

endif()
