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
	uint16_t last_seq_rx;
	uint16_t last_seq_tx;
	uint8_t retry_count;
	int64_t last_rx_ms;
	uint16_t last_rpc_seq;
	uint8_t last_rpc_resp[130];
	uint8_t last_rpc_resp_len;
};

void lora_session_init(void);
struct lora_session *lora_session_get(uint16_t node_id);
struct lora_session *lora_session_add(uint16_t node_id, const uint8_t key[16]);
void lora_session_remove(uint16_t node_id);
uint16_t lora_session_alloc_node_id(void);
void lora_session_restore(void);
void lora_session_persist(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_SESSION_H_ */
