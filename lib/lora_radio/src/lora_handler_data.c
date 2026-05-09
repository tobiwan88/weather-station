/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_data
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>

int lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	uint8_t num = payload_len / LORA_READING_SIZE;
	if (num == 0 || (payload_len % LORA_READING_SIZE) != 0) {
		LOG_WRN("invalid sensor data payload length %d", payload_len);
		return -EINVAL;
	}

	for (uint8_t i = 0; i < num; i++) {
		uint8_t type = payload[i * LORA_READING_SIZE];
		int32_t q31;
		memcpy(&q31, &payload[i * LORA_READING_SIZE + 1], sizeof(q31));

		uint32_t uid = lora_radio_uid_from_node_id((uint8_t)(src_node & 0xFF), type);

		int ret = lora_radio_publish_data(uid, type, q31);
		if (ret < 0) {
			LOG_WRN("publish failed for uid 0x%08x: %d", uid, ret);
		}
	}

	LOG_DBG("processed %d readings from node 0x%04x", num, src_node);
	return 0;
}
