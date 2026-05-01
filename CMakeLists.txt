# SPDX-License-Identifier: Apache-2.0
#
# Module-level CMakeLists.txt for weather-station. This is NOT the application
# entry point — it is loaded by Zephyr's module infrastructure (after Zephyr has
# been found) in the application's own CMakeLists.txt.

cmake_minimum_required(VERSION 3.20.0)

add_subdirectory_ifdef(CONFIG_SENSOR_EVENT lib/sensor_event)
add_subdirectory_ifdef(CONFIG_SENSOR_TRIGGER lib/sensor_trigger)
add_subdirectory_ifdef(CONFIG_SENSOR_REGISTRY lib/sensor_registry)
add_subdirectory_ifdef(CONFIG_LOCATION_REGISTRY lib/location_registry)
add_subdirectory_ifdef(CONFIG_CONFIG_CMD lib/config_cmd)
add_subdirectory_ifdef(CONFIG_FAKE_SENSORS lib/fake_sensors)
add_subdirectory_ifdef(CONFIG_SNTP_SYNC lib/sntp_sync)
add_subdirectory_ifdef(CONFIG_CLOCK_DISPLAY lib/clock_display)
add_subdirectory_ifdef(CONFIG_LVGL_DISPLAY lib/lvgl_display)
add_subdirectory_ifdef(CONFIG_HTTP_DASHBOARD lib/http_dashboard)
add_subdirectory_ifdef(CONFIG_SENSOR_EVENT_LOG lib/sensor_event_log)
add_subdirectory_ifdef(CONFIG_REMOTE_SENSOR lib/remote_sensor)
add_subdirectory_ifdef(CONFIG_FAKE_REMOTE_SENSOR lib/fake_remote_sensor)
add_subdirectory_ifdef(CONFIG_MQTT_PUBLISHER lib/mqtt_publisher)
add_subdirectory_ifdef(CONFIG_PIPE_PUBLISHER lib/pipe_publisher)
add_subdirectory_ifdef(CONFIG_PIPE_TRANSPORT lib/pipe_transport)
