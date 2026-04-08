/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sensor_event_log.c
 * @brief Logs every env_sensor_data event to the console.
 *
 * Self-registers via SYS_INIT — no call required from main.c.
 * Enable with CONFIG_SENSOR_EVENT_LOG=y.
 *
 * Log format (one line per event):
 *   [HH:MM:SS.mmm]  0x<uid>  TEMP   <value> °C
 *   [HH:MM:SS.mmm]  0x<uid>  HUM    <value> %RH
 *
 * The timestamp shown is the sample time (k_uptime_get() at sensor-read
 * time), not the log-emit time, so it reflects when the measurement was
 * actually taken.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(sensor_event_log, LOG_LEVEL_INF);

static void sensor_event_log_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
	int64_t ts = evt->timestamp_ms;
	int hh = (int)((ts / 3600000LL) % 24);
	int mm = (int)((ts / 60000LL) % 60);
	int ss = (int)((ts / 1000LL) % 60);
	int ms = (int)(ts % 1000);

	switch (evt->type) {
	case SENSOR_TYPE_TEMPERATURE:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%04x  TEMP  %7.2f °C", hh, mm, ss, ms,
			evt->sensor_uid, q31_to_temperature_c(evt->q31_value));
		break;
	case SENSOR_TYPE_HUMIDITY:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%04x  HUM   %6.1f %%RH", hh, mm, ss, ms,
			evt->sensor_uid, q31_to_humidity_pct(evt->q31_value));
		break;
	case SENSOR_TYPE_CO2:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%04x  CO2   %6.0f ppm", hh, mm, ss, ms,
			evt->sensor_uid, q31_to_co2_ppm(evt->q31_value));
		break;
	case SENSOR_TYPE_VOC:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%04x  VOC   %6.1f IAQ", hh, mm, ss, ms,
			evt->sensor_uid, q31_to_voc_iaq(evt->q31_value));
		break;
	default:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%04x  type=%-3d  q31=0x%08x", hh, mm, ss, ms,
			evt->sensor_uid, evt->type, (uint32_t)evt->q31_value);
		break;
	}
}

ZBUS_LISTENER_DEFINE(sensor_event_log_listener, sensor_event_log_cb);

static int sensor_event_log_init(void)
{
	return zbus_chan_add_obs(&sensor_event_chan, &sensor_event_log_listener, K_NO_WAIT);
}
SYS_INIT(sensor_event_log_init, APPLICATION, 95);
