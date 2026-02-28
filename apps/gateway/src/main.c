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

#include <common/weather_messages.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#if CONFIG_LVGL_DISPLAY
#	include <lvgl_display/lvgl_display.h>
#endif

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
	int64_t ts_ms = evt->timestamp_ms;
	int hh = (int)((ts_ms / 3600000LL) % 24);
	int mm = (int)((ts_ms / 60000LL) % 60);
	int ss = (int)((ts_ms / 1000LL) % 60);

	switch (evt->type) {
	case SENSOR_TYPE_TEMPERATURE:
		LOG_INF("uid=0x%04x type=temp q31=0x%08x (%.2f C) uptime=%02d:%02d:%02d",
			evt->sensor_uid, evt->q31_value, q31_to_temperature_c(evt->q31_value), hh,
			mm, ss);
		break;
	case SENSOR_TYPE_HUMIDITY:
		LOG_INF("uid=0x%04x type=hum  q31=0x%08x (%.1f %%RH) uptime=%02d:%02d:%02d",
			evt->sensor_uid, evt->q31_value, q31_to_humidity_pct(evt->q31_value), hh,
			mm, ss);
		break;
	default:
		LOG_INF("uid=0x%04x type=%d q31=0x%08x uptime=%02d:%02d:%02d", evt->sensor_uid,
			evt->type, evt->q31_value, hh, mm, ss);
		break;
	}
}

ZBUS_LISTENER_DEFINE(gateway_event_listener, gateway_event_cb);

static int gateway_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &gateway_event_listener, K_NO_WAIT);
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
#if CONFIG_LVGL_DISPLAY
	lvgl_display_run(); /* never returns; SDL must run on main thread */
#else
	k_sleep(K_FOREVER);
#endif
	return 0;
}
