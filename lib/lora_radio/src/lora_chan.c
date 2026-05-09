/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/zbus/zbus.h>

#include <lora_radio/lora_radio.h>

ZBUS_CHAN_DEFINE(lora_link_chan, struct lora_link_info, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));
