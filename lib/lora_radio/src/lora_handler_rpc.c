/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/logging/log.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>

LOG_MODULE_DECLARE(lora_radio);

void lora_handle_rpc_cmd(const struct lora_l2_header *hdr, const uint8_t *payload, size_t len)
{
	ARG_UNUSED(hdr);
	ARG_UNUSED(payload);
	ARG_UNUSED(len);

	LOG_DBG("rpc_cmd from node %u (cmd=0x%02x)", hdr->src_node[0], len > 0 ? payload[0] : 0);
}
