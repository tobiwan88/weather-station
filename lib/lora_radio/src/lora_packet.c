/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/logging/log.h>

#include <lora_radio/lora_frame.h>

LOG_MODULE_DECLARE(lora_radio);

int lora_packet_decode(const uint8_t *buf, size_t len, struct lora_l2_header *hdr,
		       const uint8_t **payload, size_t *payload_len)
{
	if (buf == NULL || hdr == NULL || payload == NULL || payload_len == NULL) {
		return -EINVAL;
	}

	if (len < sizeof(struct lora_l2_header)) {
		return -EMSGSIZE;
	}

	memcpy(hdr, buf, sizeof(struct lora_l2_header));

	*payload = buf + sizeof(struct lora_l2_header);
	*payload_len = len - sizeof(struct lora_l2_header);

	uint8_t ftype = lora_frame_type(hdr->type_ver);
	if (ftype > LORA_FRAME_ACK) {
		return -ENOTSUP;
	}

	return 0;
}
