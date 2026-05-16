/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_NODE_LORA_NODE_H_
#define LORA_NODE_LORA_NODE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LoRa radio and load session from NVS.
 *
 * Configures the radio with the default SF, BW, frequency, and TX power.
 * Loads the session (node_id, session_key) from NVS settings. If no session
 * is found, the node operates in unpaired mode (unencrypted transmissions).
 *
 * @return 0 on success, negative errno on failure.
 */
int lora_node_init(void);

/**
 * @brief Transmit a buffer of compact sensor readings over LoRa.
 *
 * Encodes the readings into a L2 frame with AES-128-GCM encryption (if
 * paired) and CRC-16 trailer, then transmits via the LoRa radio.
 *
 * @param readings     Pointer to the readings buffer (N × 5 bytes: type + Q31).
 * @param num_readings Number of readings in the buffer.
 * @return 0 on success, negative errno on failure.
 */
int lora_node_transmit(const uint8_t *readings, size_t num_readings);

/**
 * @brief Get the assigned node_id.
 *
 * @return Node ID (0 if unpaired).
 */
uint16_t lora_node_get_id(void);

/**
 * @brief Store the session (node_id, session_key) to NVS.
 *
 * Called after successful pairing to persist the session.
 *
 * @param node_id     Assigned node ID.
 * @param session_key 16-byte AES session key.
 * @return 0 on success, negative errno on failure.
 */
int lora_node_save_session(uint16_t node_id, const uint8_t session_key[16]);

#ifdef __cplusplus
}
#endif

#endif /* LORA_NODE_LORA_NODE_H_ */
