/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_INTERNAL_H_
#define LORA_RADIO_LORA_RADIO_INTERNAL_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/zbus/zbus.h>

#include <lora_radio/lora_frame.h>

/* Shared LoRa device handle — set in lora_radio_main.c, used by handlers */
extern const struct device *lora_radio_dev;

/* Packet framing — lora_packet.c */
int lora_packet_encode(struct lora_l2_header *hdr, const void *payload, uint8_t payload_len,
		       const uint8_t session_key[16], uint8_t *out_buf, uint8_t *out_len);

int lora_packet_decode(const uint8_t *in_buf, uint8_t in_len, const uint8_t session_key[16],
		       struct lora_l2_header *hdr, uint8_t *payload, uint8_t *payload_len);

#include <lora_radio/lora_session.h>

/* Protocol handlers — called from RX thread dispatch */
int lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_prov_beacon(const struct lora_l2_header *hdr, const uint8_t *payload,
			    uint8_t payload_len);

int lora_prov_init(void);

#endif /* LORA_RADIO_LORA_RADIO_INTERNAL_H_ */
