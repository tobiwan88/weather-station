/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_CHAN_H_
#define LORA_RADIO_LORA_CHAN_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lora_link_event {
	uint16_t node_id;
	int16_t rssi;
	uint8_t snr;
	uint16_t seq_num;
	uint16_t crc_errors;
};

struct lora_fota_event {
	enum { LORA_FOTA_START, LORA_FOTA_CANCEL } action;
	uint32_t target_uid;
	uint32_t image_size;
	uint8_t fota_mode;
};

ZBUS_CHAN_DECLARE(lora_link_chan);
ZBUS_CHAN_DECLARE(lora_fota_chan);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_CHAN_H_ */
