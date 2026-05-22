# Install script for directory: /__w/weather-station/zephyr

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/arch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/lib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/boards/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/subsys/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/drivers/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/weather-station/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/cmsis_6/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/hal_nordic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/hal_nxp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/hal_st/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/hal_stm32/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/loramac-node/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/mcuboot/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/nanopb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/modules/segger/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/kernel/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/cmake/flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/cmake/usage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/cmake/reports/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/__w/weather-station/weather-station/twister-out/native_sim_native_64/host_gnu/weather-station/tests/mqtt_publisher/weather_station.mqtt_publisher/zephyr/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
