/* SPDX-License-Identifier: Apache-2.0 */

#include <remote_sensor/remote_sensor.h>
#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DEFINE(remote_peer_cmd_chan, struct remote_peer_cmd_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.action = 0, .proto = 0, .target_uid = 0, .addr_len = 0));
