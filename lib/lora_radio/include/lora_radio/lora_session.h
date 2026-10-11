/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_SESSION_H_
#define LORA_RADIO_LORA_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lora_session_state {
	LORA_SESSION_UNPAIRED,
	LORA_SESSION_PAIRING,
	LORA_SESSION_PAIRED,
	LORA_SESSION_EXPIRED,
};

struct lora_session {
	uint16_t node_id;
	enum lora_session_state state;
	uint8_t session_key[16];
	uint32_t last_seq_rx;  /* RAM only: highest authenticated uplink seq */
	uint32_t last_seq_tx;  /* legacy per-node counter, unused for new frames: use
				* lora_session_next_tx_seq() */
	uint32_t replay_drops; /* uplink frames dropped as replay/duplicate */
	bool rx_seen;          /* last_seq_rx is valid (a frame was accepted since boot) */
	bool plain_warned;     /* plaintext-drop warning already logged for this node */
	uint8_t retry_count;
	int64_t last_rx_ms;
};

void lora_session_init(void);
struct lora_session *lora_session_get(uint16_t node_id);
struct lora_session *lora_session_add(uint16_t node_id, const uint8_t key[16]);
void lora_session_remove(uint16_t node_id);
uint16_t lora_session_alloc_node_id(void);
void lora_session_restore(void);
void lora_session_persist(void);

/**
 * @brief Replay protection / dedup for an authenticated uplink frame.
 *
 * Call only after the frame passed authentication. Accepts @p seq if it is
 * higher than the last accepted one and records it; otherwise counts a drop.
 * The first frame after a gateway reboot is always accepted (last_seq_rx is
 * RAM only): a replay captured before the reboot can be injected once. This is
 * a documented residual risk.
 *
 * @retval true  frame is fresh, state updated
 * @retval false frame is a replay or duplicate (seq <= last accepted)
 */
bool lora_session_rx_accept(struct lora_session *s, uint32_t seq);

/**
 * Downlink sequence counter with write-ahead reservation. Values below
 * @c reserved are covered by a persisted reservation, so after a reboot the
 * counter resumes at @c reserved and a nonce is never reused.
 */
struct lora_seq_counter {
	uint32_t next;     /* next value to hand out */
	uint32_t reserved; /* persisted upper bound (exclusive) */
};

typedef int (*lora_seq_persist_fn)(uint32_t reserved);

/** Initialise from the persisted reservation (0 if none). Sequence 0 is never issued. */
void lora_seq_counter_init(struct lora_seq_counter *c, uint32_t persisted_reserved);

/**
 * @brief Take the next value, extending the reservation by @p reserve first when needed.
 *
 * @retval 0 on success, *seq set
 * @retval -EOVERFLOW if the 32-bit space is exhausted
 * @retval <0 error from @p persist; no value is handed out
 */
int lora_seq_counter_next(struct lora_seq_counter *c, uint32_t reserve, lora_seq_persist_fn persist,
			  uint32_t *seq);

/**
 * @brief Next downlink sequence number (one persisted counter for all nodes).
 *
 * @retval 0 on success, *seq set
 * @retval <0 if the reservation could not be persisted: the caller must not
 *         transmit an encrypted frame.
 */
int lora_session_next_tx_seq(uint32_t *seq);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_SESSION_H_ */
