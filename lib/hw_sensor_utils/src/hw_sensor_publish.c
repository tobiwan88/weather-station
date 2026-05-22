/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME hw_sensor_utils
#define LOG_LEVEL       CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <hw_sensor_utils/hw_sensor_publish.h>
#include <sensor_event/sensor_event.h>
#include <zephyr/kernel.h>

int hw_sensor_publish(uint32_t sensor_uid, enum sensor_type type, int32_t q31_value)
{
	struct env_sensor_data evt = {
		.sensor_uid = sensor_uid,
		.type = type,
		.q31_value = q31_value,
		.timestamp_ms = k_uptime_get(),
	};

	int rc = zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);
	if (rc != 0) {
		LOG_WRN("pub failed (uid=0x%08x, type=%d, rc=%d)", sensor_uid, type, rc);
	}
	return rc;
}
