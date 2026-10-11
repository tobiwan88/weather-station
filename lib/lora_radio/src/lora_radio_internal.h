/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_INTERNAL_H_
#define LORA_RADIO_LORA_RADIO_INTERNAL_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/zbus/zbus.h>

#include <lora_proto/lora_packet.h>

/* Shared LoRa device handle — set in lora_radio_main.c, used by handlers */
extern const struct device *lora_radio_dev;

#include <lora_radio/lora_session.h>

/* Protocol handlers — called from RX thread dispatch */
int lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_rpc_resp(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_fota_chunk_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

/* Pending workqueue — defined in lora_pending.c, used by FOTA sender */
extern struct k_work_q pending_workq;

#endif /* LORA_RADIO_LORA_RADIO_INTERNAL_H_ */
