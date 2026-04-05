/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zbus/zbus.h>

#include <remote_sensor/remote_sensor.h>

/*
 * zbus channel ownership rule: ZBUS_CHAN_DEFINE in exactly one .c file.
 * Declared (ZBUS_CHAN_DECLARE) in remote_sensor.h for external access.
 */
ZBUS_CHAN_DEFINE(remote_scan_ctrl_chan, struct remote_scan_ctrl_event, NULL, /* validator */
		 NULL,                                                       /* user_data */
		 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
