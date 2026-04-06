/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher_format.c
 * @brief Pure format helpers for mqtt_publisher — no I/O, no MQTT, no RTOS.
 *
 * These functions are kept separate so they can be unit-tested without a
 * broker or sensor_registry stub.
 */

#include <stdio.h>
#include <string.h>

#include <sensor_event/sensor_event.h>

#include "mqtt_publisher_format.h"

const char *mqtt_publisher_type_to_topic_str(enum sensor_type t)
{
	switch (t) {
	case SENSOR_TYPE_TEMPERATURE:
		return "temperature";
	case SENSOR_TYPE_HUMIDITY:
		return "humidity";
	case SENSOR_TYPE_PRESSURE:
		return "pressure";
	case SENSOR_TYPE_CO2:
		return "co2";
	case SENSOR_TYPE_LIGHT:
		return "light";
	case SENSOR_TYPE_UV_INDEX:
		return "uv_index";
	case SENSOR_TYPE_BATTERY_MV:
		return "battery_mv";
	default:
		return "unknown";
	}
}

const char *mqtt_publisher_type_to_unit(enum sensor_type t)
{
	switch (t) {
	case SENSOR_TYPE_TEMPERATURE:
		return "\xc2\xb0\x43"; /* °C (UTF-8) */
	case SENSOR_TYPE_HUMIDITY:
		return "%";
	case SENSOR_TYPE_PRESSURE:
		return "hPa";
	case SENSOR_TYPE_CO2:
		return "ppm";
	case SENSOR_TYPE_LIGHT:
		return "lux";
	case SENSOR_TYPE_UV_INDEX:
		return "";
	case SENSOR_TYPE_BATTERY_MV:
		return "mV";
	default:
		return "";
	}
}

double mqtt_publisher_q31_to_value(enum sensor_type t, int32_t q31)
{
	switch (t) {
	case SENSOR_TYPE_TEMPERATURE:
		return q31_to_temperature_c(q31);
	case SENSOR_TYPE_HUMIDITY:
		return q31_to_humidity_pct(q31);
	default:
		return (double)q31 / (double)INT32_MAX;
	}
}

void mqtt_publisher_build_topic(const char *gateway, const char *location, const char *display_name,
				enum sensor_type type, char *buf, size_t len)
{
	if (location == NULL || location[0] == '\0') {
		location = "unknown";
	}
	if (display_name == NULL || display_name[0] == '\0') {
		display_name = "unknown";
	}

	snprintf(buf, len, "%s/%s/%s/%s", gateway, location, display_name,
		 mqtt_publisher_type_to_topic_str(type));
}

int mqtt_publisher_build_payload(int64_t epoch_s, enum sensor_type type, int32_t q31_value,
				 char *buf, size_t len)
{
	double value = mqtt_publisher_q31_to_value(type, q31_value);
	const char *unit = mqtt_publisher_type_to_unit(type);

	return snprintf(buf, len, "{\"time\":%lld,\"value\":%.2f,\"unit\":\"%s\"}",
			(long long)epoch_s, value, unit);
}
