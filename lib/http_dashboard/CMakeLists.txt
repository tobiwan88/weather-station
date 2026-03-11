# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(src/http_dashboard.c)
zephyr_library_include_directories(include)
zephyr_include_directories(include)

# Linker section for per-service HTTP resource descriptor iterable.
zephyr_linker_sources(SECTIONS http_dashboard_sections.ld)
