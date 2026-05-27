/* SPDX-License-Identifier: Apache-2.0 */

#include <lora_radio/lora_chan.h>
#include <zephyr/zbus/zbus.h>

ZBUS_CHAN_DEFINE(lora_link_chan, struct lora_link_event, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.node_id = 0, .rssi = 0, .snr = 0, .seq_num = 0, .crc_errors = 0));

ZBUS_CHAN_DEFINE(lora_fota_chan, struct lora_fota_event, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.action = 0, .target_uid = 0, .image_size = 0, .fota_mode = 0));

ZBUS_CHAN_DEFINE(lora_rpc_result_chan, struct lora_rpc_result_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.node_id = 0, .cmd_id = 0, .status = 0, .resp_data = {0},
			       .resp_len = 0));
