# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FOTA_CONFIRM)
  zephyr_library()
  zephyr_library_sources(src/fota_confirm.c)
endif()
