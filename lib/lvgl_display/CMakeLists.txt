# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LVGL_DISPLAY)
  zephyr_library()
  zephyr_library_sources(src/lvgl_display.c)
  zephyr_include_directories(include)
endif()
