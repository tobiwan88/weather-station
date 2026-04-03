/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_sensors_config_cmd.c
 * @brief config_cmd_chan listener for fake_sensors.
 *
 * Subscribes to config_cmd_chan and handles CONFIG_CMD_SET_TRIGGER_INTERVAL
 * by forwarding the new interval to fake_sensors_set_auto_publish_ms().
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <config_cmd/config_cmd.h>
#include <fake_sensors/fake_sensors.h>

LOG_MODULE_REGISTER(fake_sensors_config_cmd, LOG_LEVEL_INF);

static void config_cmd_cb(const struct zbus_channel *chan)
{
	const struct config_cmd_event *evt = zbus_chan_const_msg(chan);

	if (evt->cmd == CONFIG_CMD_SET_TRIGGER_INTERVAL) {
		fake_sensors_set_auto_publish_ms(evt->arg);
	}
}

ZBUS_LISTENER_DEFINE(fake_sensors_config_cmd_listener, config_cmd_cb);

static int fake_sensors_config_cmd_init(void)
{
	return zbus_chan_add_obs(&config_cmd_chan, &fake_sensors_config_cmd_listener, K_NO_WAIT);
}

SYS_INIT(fake_sensors_config_cmd_init, APPLICATION, 95);
