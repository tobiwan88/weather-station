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

LOG_MODULE_REGISTER(sensor_event_log, CONFIG_SENSOR_EVENT_LOG_LOG_LEVEL);

/*
 * The gateway build does not enable FP printf for logs, so values are
 * converted to scaled integers here and printed with integer formats only.
 */
static int32_t scaled_round(double v, double scale)
{
	double s = v * scale;

	return (int32_t)(s >= 0.0 ? s + 0.5 : s - 0.5);
}

static void sensor_event_log_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
	int64_t ts = evt->timestamp_ms;
	int hh = (int)((ts / 3600000LL) % 24);
	int mm = (int)((ts / 60000LL) % 60);
	int ss = (int)((ts / 1000LL) % 60);
	int ms = (int)(ts % 1000);

	switch (evt->type) {
	case SENSOR_TYPE_TEMPERATURE: {
		int32_t centi = scaled_round(q31_to_temperature_c(evt->q31_value), 100.0);
		uint32_t a = (uint32_t)(centi < 0 ? -centi : centi);

		LOG_INF("[%02d:%02d:%02d.%03d]  0x%08x  TEMP  %s%u.%02u °C", hh, mm, ss, ms,
			evt->sensor_uid, centi < 0 ? "-" : "", a / 100U, a % 100U);
		break;
	}
	case SENSOR_TYPE_HUMIDITY: {
		int32_t deci = scaled_round(q31_to_humidity_pct(evt->q31_value), 10.0);
		uint32_t a = (uint32_t)(deci < 0 ? -deci : deci);

		LOG_INF("[%02d:%02d:%02d.%03d]  0x%08x  HUM   %s%u.%u %%RH", hh, mm, ss, ms,
			evt->sensor_uid, deci < 0 ? "-" : "", a / 10U, a % 10U);
		break;
	}
	case SENSOR_TYPE_CO2:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%08x  CO2   %d ppm", hh, mm, ss, ms,
			evt->sensor_uid, scaled_round(q31_to_co2_ppm(evt->q31_value), 1.0));
		break;
	case SENSOR_TYPE_VOC: {
		int32_t deci = scaled_round(q31_to_voc_iaq(evt->q31_value), 10.0);
		uint32_t a = (uint32_t)(deci < 0 ? -deci : deci);

		LOG_INF("[%02d:%02d:%02d.%03d]  0x%08x  VOC   %s%u.%u IAQ", hh, mm, ss, ms,
			evt->sensor_uid, deci < 0 ? "-" : "", a / 10U, a % 10U);
		break;
	}
	default:
		LOG_INF("[%02d:%02d:%02d.%03d]  0x%08x  type=%-3d  q31=0x%08x", hh, mm, ss, ms,
			evt->sensor_uid, evt->type, (uint32_t)evt->q31_value);
		break;
	}
}

ZBUS_LISTENER_DEFINE(sensor_event_log_listener, sensor_event_log_cb);

static int sensor_event_log_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &sensor_event_log_listener, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("failed to subscribe to sensor_event_chan: %d", rc);
		return rc;
	}
	LOG_DBG("sensor_event_log: init done");
	return 0;
}
SYS_INIT(sensor_event_log_init, APPLICATION, 95);
