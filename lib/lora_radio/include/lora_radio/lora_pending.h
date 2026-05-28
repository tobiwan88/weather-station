/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_PENDING_H_
#define LORA_RADIO_LORA_PENDING_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the reliable TX subsystem.
 *
 * Creates the pending workqueue and zeros all pending slots.
 * Called from lora_radio_init().
 */
void lora_pending_init(void);

/**
 * @brief Send a frame reliably (blocking).
 *
 * Transmits the frame and blocks until a matching ACK/RESP is received
 * or retries are exhausted. Caller MUST be in a thread context (not
 * a zbus listener).
 *
 * @param node_id     Target LoRa node.
 * @param seq_num     Sequence number of the sent frame.
 * @param frame_type  Frame type (LORA_FRAME_RPC_CMD or LORA_FRAME_FOTA_CHUNK).
 * @param tx_buf      Encoded frame (L2 header + encrypted payload).
 * @param tx_len      Frame length.
 * @return 0 on success, negative errno on failure (-ETIMEDOUT on retry exhaustion).
 */
int lora_pending_send(uint16_t node_id, uint16_t seq_num, uint8_t frame_type, const uint8_t *tx_buf,
		      uint8_t tx_len);

/**
 * @brief Send a frame reliably (non-blocking).
 *
 * Enqueues the frame for reliable transmission and returns immediately.
 * The result is published on lora_rpc_result_chan when resolved.
 * Safe for zbus listener context.
 *
 * @param node_id     Target LoRa node.
 * @param seq_num     Sequence number of the sent frame.
 * @param frame_type  Frame type.
 * @param tx_buf      Encoded frame.
 * @param tx_len      Frame length.
 * @return 0 on success (enqueued), negative errno if no slot available.
 */
int lora_pending_send_async(uint16_t node_id, uint16_t seq_num, uint8_t frame_type,
			    const uint8_t *tx_buf, uint8_t tx_len);

/**
 * @brief Match a received ACK/RESP to a pending request.
 *
 * Called from the RX thread dispatch when an ACK, RPC_RESP, or
 * FOTA_CHUNK_ACK is received. Signals the pending slot if seq_num
 * and node_id match.
 *
 * @param node_id   Source node of the ACK.
 * @param seq_num   Sequence number from the ACK.
 * @param status    0=OK, positive error code from ACK payload.
 */
void lora_pending_ack_match(uint16_t node_id, uint16_t seq_num, int status);

/**
 * @brief Cancel all pending requests for a node.
 *
 * Called when a session is removed. Cancels any in-flight retry
 * timers and marks the slot inactive.
 *
 * @param node_id Target LoRa node.
 */
void lora_pending_cancel(uint16_t node_id);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_PENDING_H_ */
