/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_INTERNAL_H_
#define LORA_RADIO_LORA_RADIO_INTERNAL_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#include <lora_radio/lora_frame.h>

/* Packet framing — lora_packet.c */
int lora_packet_decode(const uint8_t *in_buf, uint8_t in_len, const uint8_t session_key[16],
		       struct lora_l2_header *hdr, uint8_t *payload, uint8_t *payload_len);

#include <lora_radio/lora_session.h>

/* Protocol handlers — called from RX thread dispatch */
int lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_prov_beacon(const struct lora_l2_header *hdr, const uint8_t *payload,
			    uint8_t payload_len);

#endif /* LORA_RADIO_LORA_RADIO_INTERNAL_H_ */
