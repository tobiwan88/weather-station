/* SPDX-License-Identifier: Apache-2.0 */

#include <config_cmd/config_cmd.h>
#include <zephyr/zbus/zbus.h>

/**
 * @brief zbus channel carrying config_cmd_event messages.
 *
 * Ownership: defined here; the header exposes the extern declaration.
 * Any module may publish a command; consumers subscribe as listeners.
 */
ZBUS_CHAN_DEFINE(config_cmd_chan,         /* name             */
		 struct config_cmd_event, /* message type     */
		 NULL,                    /* validator (none) */
		 NULL,                    /* user data        */
		 ZBUS_OBSERVERS_EMPTY,    /* initial observers */
		 ZBUS_MSG_INIT(.cmd = CONFIG_CMD_SET_TRIGGER_INTERVAL, .arg = 0));
