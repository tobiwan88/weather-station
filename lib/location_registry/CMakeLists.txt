# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LOCATION_REGISTRY)

  zephyr_library()
  zephyr_library_sources(src/location_registry.c)
  zephyr_library_sources_ifdef(CONFIG_LOCATION_REGISTRY_SHELL
                               src/location_registry_shell.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
