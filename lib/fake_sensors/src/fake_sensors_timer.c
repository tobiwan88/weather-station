/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_sensors_timer.c
 * @brief Auto-publish timer for fake sensors with runtime interval control.
 *
 * Broadcasts a TRIGGER_SOURCE_TIMER event on sensor_trigger_chan at the
 * configured interval. The interval can be changed at runtime via
 * fake_sensors_set_auto_publish_ms().
 *
 * Replaces the per-driver timer that was previously in fake_temperature.c.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <fake_sensors/fake_sensors.h>
#include <sensor_trigger/sensor_trigger.h>

LOG_MODULE_REGISTER(fake_sensors_timer, CONFIG_FAKE_SENSORS_LOG_LEVEL);

static void auto_timer_cb(struct k_timer *timer);

K_TIMER_DEFINE(g_auto_timer, auto_timer_cb, NULL);

static void auto_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	struct sensor_trigger_event trig = {
		.source = TRIGGER_SOURCE_TIMER,
		.target_uid = 0,
	};
	zbus_chan_pub(&sensor_trigger_chan, &trig, K_NO_WAIT);
}

void fake_sensors_set_auto_publish_ms(uint32_t ms)
{
	if (ms > 0) {
		k_timer_start(&g_auto_timer, K_MSEC(ms), K_MSEC(ms));
		LOG_INF("auto-publish interval set to %u ms", ms);
	} else {
		k_timer_stop(&g_auto_timer);
		LOG_INF("auto-publish disabled");
	}
}

static int fake_sensors_timer_init(void)
{
#if CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS > 0
	k_timer_start(&g_auto_timer, K_MSEC(CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS),
		      K_MSEC(CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS));
	LOG_INF("auto-publish started at %d ms", CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS);
#endif
	return 0;
}

SYS_INIT(fake_sensors_timer_init, APPLICATION, 99);
