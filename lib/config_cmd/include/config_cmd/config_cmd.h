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
	/** Change auto-publish interval; arg = interval in ms (0 = disable). */
	CONFIG_CMD_SET_TRIGGER_INTERVAL,
	/** Trigger an immediate SNTP resync; arg is unused. */
	CONFIG_CMD_SNTP_RESYNC,
};

/**
 * @brief Command event transmitted on config_cmd_chan.
 */
struct config_cmd_event {
	enum config_cmd_type cmd; /**< Which command to execute */
	uint32_t arg;             /**< Command argument (see enum above) */
};

/** zbus channel carrying config_cmd_event (defined in config_cmd.c). */
ZBUS_CHAN_DECLARE(config_cmd_chan);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_CMD_CONFIG_CMD_H_ */
