/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zbus/zbus.h>
#include <sensor_trigger/sensor_trigger.h>

/**
 * @brief zbus channel carrying sensor_trigger_event messages.
 *
 * Ownership: defined here; the header exposes the extern declaration.
 * Any module may publish a trigger; sensor drivers subscribe as listeners.
 */
ZBUS_CHAN_DEFINE(sensor_trigger_chan,          /* name                */
		 struct sensor_trigger_event,  /* message type        */
		 NULL,                         /* validator (none)    */
		 NULL,                         /* user data           */
		 ZBUS_OBSERVERS_EMPTY,         /* initial observers   */
		 ZBUS_MSG_INIT(.source     = TRIGGER_SOURCE_STARTUP,
			       .target_uid = 0));
