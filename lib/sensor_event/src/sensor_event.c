/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <sensor_event/sensor_event.h>
#include <zephyr/zbus/zbus.h>

/* Non-inline decode functions referenced by the descriptor table. */
static double decode_temperature(int32_t q31)
{
	return q31_to_temperature_c(q31);
}

static double decode_humidity(int32_t q31)
{
	return q31_to_humidity_pct(q31);
}

static double decode_generic(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX;
}

static const struct sensor_type_desc descs[] = {
	[SENSOR_TYPE_TEMPERATURE] = {"\xc2\xb0\x43", decode_temperature}, /* °C */
	[SENSOR_TYPE_HUMIDITY] = {"%", decode_humidity},
	[SENSOR_TYPE_PRESSURE] = {"hPa", decode_generic},
	[SENSOR_TYPE_CO2] = {"ppm", decode_generic},
	[SENSOR_TYPE_LIGHT] = {"lux", decode_generic},
	[SENSOR_TYPE_UV_INDEX] = {"", decode_generic},
	[SENSOR_TYPE_BATTERY_MV] = {"mV", decode_generic},
};

static const struct sensor_type_desc fallback = {"", decode_generic};

const struct sensor_type_desc *sensor_type_get_desc(enum sensor_type t)
{
	if ((unsigned int)t >= ARRAY_SIZE(descs)) {
		return &fallback;
	}
	return &descs[t];
}

const char *sensor_type_to_unit(enum sensor_type t)
{
	return sensor_type_get_desc(t)->unit;
}

/* Sanity check: struct must contain no pointers and fit in a cache line. */
BUILD_ASSERT(sizeof(struct env_sensor_data) <= 32, "env_sensor_data too large");

/**
 * @brief zbus channel carrying env_sensor_data events.
 *
 * Ownership: defined here; the header exposes the extern declaration.
 * Validators: none (open channel — any driver may publish).
 */
ZBUS_CHAN_DEFINE(sensor_event_chan,      /* name                      */
		 struct env_sensor_data, /* message type              */
		 NULL,                   /* validator (none)          */
		 NULL,                   /* user data                 */
		 ZBUS_OBSERVERS_EMPTY,   /* initial observers         */
		 ZBUS_MSG_INIT(.sensor_uid = 0, .type = SENSOR_TYPE_TEMPERATURE, .q31_value = 0,
			       .timestamp_ms = 0));
