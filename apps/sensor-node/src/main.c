/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (sensor-node)
 * @brief Sensor-node application entry point.
 *
 * Per RULE 4: main.c registers the log module and sleeps forever.
 * All application logic (sensor drivers, shell) is initialised via
 * SYS_INIT callbacks in the library modules, activated by the Kconfig
 * options in prj.conf.
 *
 * The sensor node listens on sensor_trigger_chan (fired by an external
 * trigger source such as a button or a LoRa command) and publishes
 * env_sensor_data events on sensor_event_chan. The LoRa radio driver
 * (when implemented) will forward those events to the gateway.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <common/weather_messages.h>

LOG_MODULE_REGISTER(sensor_node, LOG_LEVEL_INF);

/**
 * @brief zbus listener that logs local sensor events for debug purposes.
 */
static void node_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	switch (evt->type) {
	case SENSOR_TYPE_TEMPERATURE:
		LOG_INF("uid=0x%04x temp=%.2f C ts=%lld ms", evt->sensor_uid,
			q31_to_temperature_c(evt->q31_value), evt->timestamp_ms);
		break;
	case SENSOR_TYPE_HUMIDITY:
		LOG_INF("uid=0x%04x hum=%.1f %%RH ts=%lld ms", evt->sensor_uid,
			q31_to_humidity_pct(evt->q31_value), evt->timestamp_ms);
		break;
	default:
		LOG_INF("uid=0x%04x type=%d q31=0x%08x ts=%lld ms", evt->sensor_uid, evt->type,
			evt->q31_value, evt->timestamp_ms);
		break;
	}
}

ZBUS_LISTENER_DEFINE(node_event_listener, node_event_cb);

static int node_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &node_event_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to subscribe to sensor_event_chan: %d", rc);
	}
	LOG_INF("Sensor node started.");
	return rc;
}
SYS_INIT(node_init, APPLICATION, 95);

int main(void)
{
	LOG_INF("weather-station sensor-node v0.1.0");
	k_sleep(K_FOREVER);
	return 0;
}
