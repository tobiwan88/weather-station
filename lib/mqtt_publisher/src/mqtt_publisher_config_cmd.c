/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher_config_cmd.c
 * @brief Subscribes to config_cmd_chan to allow runtime MQTT reconfiguration.
 *
 * Receives commands from the HTTP dashboard (or any other producer) and
 * applies them via the public mqtt_publisher API.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <config_cmd/config_cmd.h>
#include <mqtt_publisher/mqtt_publisher.h>

LOG_MODULE_DECLARE(mqtt_publisher, CONFIG_MQTT_PUBLISHER_LOG_LEVEL);

static void mqtt_config_cmd_cb(const struct zbus_channel *chan)
{
	const struct config_cmd_event *cmd = zbus_chan_const_msg(chan);

	switch (cmd->cmd) {
	case CONFIG_CMD_MQTT_SET_ENABLED:
		mqtt_publisher_set_enabled(cmd->arg != 0);
		break;

	case CONFIG_CMD_MQTT_SET_BROKER: {
		struct mqtt_publisher_config cfg;

		mqtt_publisher_get_config(&cfg);
		/* Only overwrite fields that the producer explicitly set (non-zero/non-empty). */
		if (cmd->data.broker.host[0] != '\0') {
			strncpy(cfg.host, cmd->data.broker.host, sizeof(cfg.host) - 1);
			cfg.host[sizeof(cfg.host) - 1] = '\0';
		}
		if (cmd->data.broker.port != 0) {
			cfg.port = cmd->data.broker.port;
		}
		if (cmd->data.broker.keepalive != 0) {
			cfg.keepalive = cmd->data.broker.keepalive;
		}
		mqtt_publisher_set_broker(&cfg);
		break;
	}

	case CONFIG_CMD_MQTT_SET_AUTH:
		mqtt_publisher_set_auth(cmd->data.auth.username, cmd->data.auth.password);
		break;

	case CONFIG_CMD_MQTT_SET_GATEWAY:
		mqtt_publisher_set_gateway_name(cmd->data.gateway_name);
		break;

	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(mqtt_config_cmd_listener, mqtt_config_cmd_cb);

static int mqtt_config_cmd_init(void)
{
	return zbus_chan_add_obs(&config_cmd_chan, &mqtt_config_cmd_listener, K_NO_WAIT);
}

SYS_INIT(mqtt_config_cmd_init, APPLICATION, 98);
