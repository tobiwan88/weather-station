/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_H_
#define LORA_RADIO_LORA_RADIO_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Transport metadata (iterable section — no function pointers)
 * --------------------------------------------------------------------------
 * Registered via REMOTE_TRANSPORT_REGISTER() in the lora_transport_info
 * iterable section. remote_sensor_manager reads these at boot for
 * bookkeeping. All runtime communication is through zbus channels.
 */

struct lora_transport_info {
	const char *name;
	uint8_t proto;
	uint32_t caps;
};

#define REMOTE_TRANSPORT_REGISTER(inst, ...)                                                       \
	STRUCT_SECTION_ITERABLE(lora_transport_info, inst) = __VA_ARGS__

/* --------------------------------------------------------------------------
 * lora_link_chan — link diagnostics
 * --------------------------------------------------------------------------
 * Published by the RX thread on every received LoRa packet. Carries RSSI,
 * SNR, source node, and sequence number for diagnostic consumers.
 */

struct lora_link_info {
	int16_t rssi;
	int8_t snr;
	uint8_t src_node;
	uint8_t len;
};

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief Publish a decoded sensor reading to sensor_event_chan.
 *
 * Builds an env_sensor_data event from a remote LoRa sensor reading and
 * publishes it on sensor_event_chan. The timestamp is assigned here using
 * k_uptime_get() to match the local clock.
 *
 * @param uid       Sensor UID (derived from node_id + sensor_type).
 * @param type      Physical quantity.
 * @param q31_value Q31-encoded measurement value.
 * @return 0 on success, negative errno on failure.
 */
int lora_radio_publish_data(uint32_t uid, enum sensor_type type, int32_t q31_value);

/**
 * @brief Send an RPC command to a remote sensor node.
 *
 * Constructs an RPC_CMD frame and transmits it over the LoRa radio. For
 * reliable commands (ACK_REQ set), a retry mechanism is used.
 *
 * @param node_id    Target node identifier (0-255).
 * @param cmd_id     RPC command ID (LORA_RPC_*).
 * @param params     Command-specific parameter bytes (may be NULL).
 * @param params_len Number of valid parameter bytes.
 * @return 0 on success, negative errno on failure.
 */
int lora_radio_rpc_send(uint16_t node_id, uint8_t cmd_id, const uint8_t *params, uint8_t param_len);

/**
 * @brief Send an RPC command reliably (non-blocking).
 *
 * Enqueues the RPC for reliable transmission and returns immediately.
 * Result published on lora_rpc_result_chan. Safe for zbus listener context.
 *
 * @param node_id    Target node identifier (0-255).
 * @param cmd_id     RPC command ID (LORA_RPC_*).
 * @param params     Command-specific parameter bytes (may be NULL).
 * @param params_len Number of valid parameter bytes.
 * @return 0 on success (enqueued), negative errno on failure.
 */
int lora_radio_rpc_send_async(uint16_t node_id, uint8_t cmd_id, const uint8_t *params,
			      uint8_t param_len);

/**
 * @brief Derive a stable sensor UID from a LoRa node_id.
 *
 * uid = (0x0200 << 16) | (node_id << 4) | (type & 0x0F)
 *
 * @param node_id LoRa node identifier (0-255).
 * @param type    Sensor type for the lower 4 bits.
 * @return Derived 32-bit UID.
 */
uint32_t lora_radio_uid_from_node_id(uint8_t node_id, enum sensor_type type);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_RADIO_H_ */
