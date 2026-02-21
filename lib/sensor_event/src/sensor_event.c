/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zbus/zbus.h>
#include <sensor_event/sensor_event.h>

/* Compile-time size check: 4 (uid) + 4 (type enum) + 4 (q31) + 8 (ts) = 20 */
BUILD_ASSERT(sizeof(struct env_sensor_data) == 20,
	     "env_sensor_data must be exactly 20 bytes");

/**
 * @brief zbus channel carrying env_sensor_data events.
 *
 * Ownership: defined here; the header exposes the extern declaration.
 * Validators: none (open channel — any driver may publish).
 */
ZBUS_CHAN_DEFINE(sensor_event_chan,          /* name                      */
		 struct env_sensor_data,     /* message type              */
		 NULL,                       /* validator (none)          */
		 NULL,                       /* user data                 */
		 ZBUS_OBSERVERS_EMPTY,       /* initial observers         */
		 ZBUS_MSG_INIT(.sensor_uid = 0,
			       .type       = SENSOR_TYPE_TEMPERATURE,
			       .q31_value  = 0,
			       .timestamp_ms = 0));
