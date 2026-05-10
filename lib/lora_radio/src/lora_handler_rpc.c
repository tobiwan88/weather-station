/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_rpc
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>

int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	(void)src_node;
	(void)payload;
	(void)payload_len;

	LOG_DBG("rpc_cmd from node %u (cmd=0x%02x)", src_node, payload_len > 0 ? payload[0] : 0);
	return 0;
}
