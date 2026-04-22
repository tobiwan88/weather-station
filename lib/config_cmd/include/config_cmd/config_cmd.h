/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file config_cmd.h
 * @brief Configuration command channel for the weather-station.
 *
 * Any producer (e.g. http_dashboard handling a POST /api/config) may
 * publish a config_cmd_event on config_cmd_chan. Each consumer
 * (fake_sensors, sntp_sync) independently subscribes and handles the
 * relevant commands. Producers never know who their consumers are.
 */

#ifndef CONFIG_CMD_CONFIG_CMD_H_
#define CONFIG_CMD_CONFIG_CMD_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Type of configuration command. */
enum config_cmd_type {
	CONFIG_CMD_SET_TRIGGER_INTERVAL,
	CONFIG_CMD_SNTP_RESYNC,
	CONFIG_CMD_MQTT_SET_ENABLED,
	CONFIG_CMD_MQTT_SET_BROKER,
	CONFIG_CMD_MQTT_SET_AUTH,
	CONFIG_CMD_MQTT_SET_GATEWAY,
};

struct config_cmd_mqtt_broker {
	char host[64];
	uint16_t port;
	uint16_t keepalive;
};

struct config_cmd_mqtt_auth {
	char username[32];
	char password[64];
};

struct config_cmd_event {
	enum config_cmd_type cmd;
	uint32_t arg;
	union {
		struct config_cmd_mqtt_broker broker;
		struct config_cmd_mqtt_auth auth;
		char gateway_name[32];
	} data;
};

/**
 * @brief Command event transmitted on config_cmd_chan.
 */

/** zbus channel carrying config_cmd_event (defined in config_cmd.c). */
ZBUS_CHAN_DECLARE(config_cmd_chan);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_CMD_CONFIG_CMD_H_ */
