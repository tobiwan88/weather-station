/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sntp_sync_config_cmd.c
 * @brief config_cmd_chan listener for sntp_sync.
 *
 * Subscribes to config_cmd_chan and handles CONFIG_CMD_SNTP_RESYNC by
 * calling sntp_sync_trigger_resync().
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <config_cmd/config_cmd.h>
#include <sntp_sync/sntp_sync.h>

LOG_MODULE_REGISTER(sntp_sync_config_cmd, CONFIG_SNTP_SYNC_LOG_LEVEL);

static void config_cmd_cb(const struct zbus_channel *chan)
{
	const struct config_cmd_event *evt = zbus_chan_const_msg(chan);

	if (evt->cmd == CONFIG_CMD_SNTP_RESYNC) {
		LOG_DBG("config_cmd: SNTP_RESYNC received");
		sntp_sync_trigger_resync();
	}
}

ZBUS_LISTENER_DEFINE(sntp_sync_config_cmd_listener, config_cmd_cb);

static int sntp_sync_config_cmd_init(void)
{
	int rc = zbus_chan_add_obs(&config_cmd_chan, &sntp_sync_config_cmd_listener, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("failed to subscribe to config_cmd_chan: %d", rc);
		return rc;
	}
	LOG_DBG("sntp_sync_config_cmd: init done");
	return 0;
}

SYS_INIT(sntp_sync_config_cmd_init, APPLICATION, 95);
