/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (gateway)
 * @brief Gateway application entry point.
 *
 * Per RULE 4: main.c registers the log module and sleeps forever.
 * All application logic (sensor drivers, shell, auto-publish timer)
 * is initialised via SYS_INIT callbacks in the library modules,
 * activated by the Kconfig options in prj.conf.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <common/weather_messages.h>

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

/**
 * @brief zbus subscriber that logs every env_sensor_data event received.
 *
 * The gateway acts as a local data sink — it logs all sensor readings
 * from sensor_event_chan so they are visible in the console output.
 */
static void gateway_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
	const char *type_str;

	switch (evt->type) {
	case SENSOR_TYPE_TEMPERATURE:
		type_str = "temp";
		LOG_INF("uid=0x%04x type=%-4s q31=0x%08x (%.2f C) ts=%lld ms",
			evt->sensor_uid, type_str, evt->q31_value,
			q31_to_temperature_c(evt->q31_value),
			evt->timestamp_ms);
		break;
	case SENSOR_TYPE_HUMIDITY:
		type_str = "hum";
		LOG_INF("uid=0x%04x type=%-4s q31=0x%08x (%.1f %%RH) ts=%lld ms",
			evt->sensor_uid, type_str, evt->q31_value,
			q31_to_humidity_pct(evt->q31_value),
			evt->timestamp_ms);
		break;
	default:
		LOG_INF("uid=0x%04x type=%d q31=0x%08x ts=%lld ms",
			evt->sensor_uid, evt->type, evt->q31_value,
			evt->timestamp_ms);
		break;
	}
}

ZBUS_LISTENER_DEFINE(gateway_event_listener, gateway_event_cb);

static int gateway_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan,
				   &gateway_event_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to subscribe to sensor_event_chan: %d", rc);
	}
	LOG_INF("Gateway started. Listening for sensor events.");
	return rc;
}
SYS_INIT(gateway_init, APPLICATION, 95);

int main(void)
{
	LOG_INF("weather-station gateway v0.1.0");
	k_sleep(K_FOREVER);
	return 0;
}
