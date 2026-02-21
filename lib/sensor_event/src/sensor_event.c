/* SPDX-License-Identifier: Apache-2.0 */

#include <sensor_event/sensor_event.h>
#include <zephyr/zbus/zbus.h>

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
