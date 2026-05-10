/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fota
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_radio_internal.h>
#include <lora_radio/lora_radio_ops.h>
#include <lora_radio/lora_session.h>

#if defined(CONFIG_IMG_MANAGER) && defined(CONFIG_FLASH_MAP)
#	include <zephyr/dfu/flash_img.h>

static struct flash_img_context fota_ctx;
static uint32_t fota_expected_offset;
static bool fota_active;
#endif

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
#if !defined(CONFIG_IMG_MANAGER) || !defined(CONFIG_FLASH_MAP)
	(void)src_node;
	(void)payload;
	(void)payload_len;
	LOG_ERR("FOTA not configured (need IMG_MANAGER + FLASH_MAP)");
	return -ENOTSUP;
#else
	if (payload_len < 5) {
		return -EINVAL;
	}

	uint32_t offset;
	memcpy(&offset, payload, sizeof(offset));
	const uint8_t *data = payload + 4;
	uint8_t data_len = payload_len - 4;
	int ret = 0;

	if (!fota_active) {
		flash_img_init(&fota_ctx, FIXED_PARTITION_ID(PM_MCUBOOT_SECONDARY));
		fota_active = true;
		fota_expected_offset = 0;
	}

	if (offset != fota_expected_offset) {
		LOG_WRN("FOTA offset mismatch: expected %u, got %u", fota_expected_offset, offset);
		goto send_ack;
	}

	ret = flash_img_buffered_write(&fota_ctx, data, data_len, false);
	if (ret < 0) {
		LOG_ERR("flash write failed at offset %u: %d", offset, ret);
		goto send_ack;
	}
	fota_expected_offset = offset + data_len;

send_ack: {
	struct lora_fota_chunk_ack ack;

	memcpy(ack.offset, &offset, sizeof(ack.offset));
	ack.status = (ret < 0) ? 1 : 0;

	struct lora_l2_header hdr;

	hdr.type_ver = (LORA_FRAME_FOTA_CHUNK_ACK << 4) | 0x01;
	hdr.flags = LORA_FLAG_ENCRYPTED;
	hdr.src_node[0] = 0;
	hdr.src_node[1] = 0;
	hdr.dst_node[0] = (uint8_t)(src_node & 0xFF);
	hdr.dst_node[1] = (uint8_t)((src_node >> 8) & 0xFF);
	hdr.seq_num[0] = 0;
	hdr.seq_num[1] = 0;

	struct lora_session *s = lora_session_get(src_node);
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;

	lora_packet_encode(&hdr, &ack, sizeof(ack), s ? s->session_key : NULL, tx_buf, &tx_len);
	lora_radio_ops->tx(tx_buf, tx_len);
}
	return 0;
#endif
}
