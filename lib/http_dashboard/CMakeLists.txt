# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(src/http_dashboard.c)
zephyr_library_include_directories(include)
zephyr_include_directories(include)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)
zephyr_library_include_directories(${gen_dir})

set(bundle_script ${CMAKE_CURRENT_SOURCE_DIR}/bundle.py)

# Bundle and embed dashboard page
set(dashboard_bundled ${gen_dir}/dashboard.bundled.html)
add_custom_command(
  OUTPUT ${dashboard_bundled}
  COMMAND
    ${PYTHON_EXECUTABLE} ${bundle_script} --html
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.html --css
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.css --js
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.js --out ${dashboard_bundled}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.html
          ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.css
          ${CMAKE_CURRENT_SOURCE_DIR}/src/dashboard.js ${bundle_script}
  COMMENT "Bundling dashboard HTML")
generate_inc_file_for_target(app ${dashboard_bundled}
                             ${gen_dir}/dashboard.html.inc)

# Bundle and embed config page
set(config_bundled ${gen_dir}/config.bundled.html)
add_custom_command(
  OUTPUT ${config_bundled}
  COMMAND
    ${PYTHON_EXECUTABLE} ${bundle_script} --html
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.html --css
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.css --js
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.js --out ${config_bundled}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/config.html
          ${CMAKE_CURRENT_SOURCE_DIR}/src/config.css
          ${CMAKE_CURRENT_SOURCE_DIR}/src/config.js ${bundle_script}
  COMMENT "Bundling config HTML")
generate_inc_file_for_target(app ${config_bundled} ${gen_dir}/config.html.inc)

# Linker section for per-service HTTP resource descriptor iterable.
zephyr_linker_sources(SECTIONS http_dashboard_sections.ld)
