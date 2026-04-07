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
	const struct sensor_type_desc *desc = sensor_type_get_desc(type);

	return snprintf(buf, len, "{\"time\":%lld,\"value\":%.2f,\"unit\":\"%s\"}",
			(long long)epoch_s, desc->decode_q31(q31_value), desc->unit);
}
