/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_pending
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/zbus/zbus.h>

#include "lora_radio_internal.h"
#include <lora_proto/lora_packet.h>
#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_pending.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

/* --------------------------------------------------------------------------
 * SF-dependent timeout scaling factors (multiplied by base timeout)
 * -------------------------------------------------------------------------- */

static int sf_timeout_factor(int sf)
{
	switch (sf) {
	case 7:
		return 1;
	case 8:
		return 14; /* 1.4x */
	case 9:
		return 2;
	case 10:
		return 3;
	case 11:
		return 44; /* 4.4x */
	case 12:
		return 6;
	default:
		return 3; /* default SF10 */
	}
}

/* --------------------------------------------------------------------------
 * Pending request slot — one per node, window size 1
 *
 * slot_lock guards the slot state machine and the sent-seq history. States:
 *   FREE   -> ACTIVE  slot_claim()
 *   ACTIVE -> DONE    exactly one winner (ACK match, timeout, error, cancel)
 *   DONE   -> FREE    after the completion was delivered
 * The winner of the ACTIVE -> DONE transition owns delivery. ACK/RESP matchers
 * (RX thread) only record the result and hand delivery to the pending
 * workqueue (needs_deliver); everyone else delivers directly.
 * -------------------------------------------------------------------------- */

enum slot_state {
	SLOT_FREE,
	SLOT_ACTIVE,
	SLOT_DONE,
};

/* Initial transmission plus one re-encoded copy per retry */
#define SENT_SEQS_MAX (CONFIG_LORA_RADIO_RETRY_MAX + 1)

struct lora_pending_slot {
	struct k_work_delayable retry_work;
	struct k_sem *done_sem; /* NULL for async, non-NULL for blocking */
	lora_pending_cb_t cb;
	void *user_data;
	uint16_t node_id;
	uint32_t sent_seqs[SENT_SEQS_MAX]; /* seq of every copy sent; an ACK of any matches */
	uint8_t n_seqs;
	uint8_t frame_type;
	uint8_t retries_left;
	int32_t base_timeout_ms;
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	enum slot_state state;
	bool needs_deliver; /* DONE by an RX-thread match: the workqueue delivers */
	int response_status;
};

static struct lora_pending_slot pending_slots[CONFIG_LORA_RADIO_SESSION_MAX];
static struct k_spinlock slot_lock;
struct k_work_q pending_workq;
static K_THREAD_STACK_DEFINE(pending_wq_stack, 1024);

/* Claim a free slot for @p node_id and fill it. Returns NULL with *err set. */
static struct lora_pending_slot *slot_claim(uint16_t node_id, uint32_t seq, uint8_t frame_type,
					    const uint8_t *tx_buf, uint8_t tx_len,
					    int32_t timeout_ms, struct k_sem *sem,
					    lora_pending_cb_t cb, void *user_data, int *err)
{
	struct lora_pending_slot *found = NULL;

	*err = 0;
	K_SPINLOCK(&slot_lock)
	{
		struct lora_pending_slot *free_slot = NULL;

		for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
			struct lora_pending_slot *p = &pending_slots[i];

			if (p->state != SLOT_FREE) {
				if (p->node_id == node_id) {
					*err = -EBUSY;
					free_slot = NULL;
					break;
				}
			} else if (free_slot == NULL) {
				free_slot = p;
			}
		}
		if (*err == 0 && free_slot == NULL) {
			*err = -ENOMEM;
		}
		if (*err == 0) {
			free_slot->done_sem = sem;
			free_slot->cb = cb;
			free_slot->user_data = user_data;
			free_slot->node_id = node_id;
			free_slot->sent_seqs[0] = seq;
			free_slot->n_seqs = 1;
			free_slot->frame_type = frame_type;
			free_slot->retries_left = CONFIG_LORA_RADIO_RETRY_MAX;
			free_slot->base_timeout_ms = timeout_ms;
			free_slot->tx_len = tx_len;
			free_slot->needs_deliver = false;
			free_slot->response_status = 0;
			memcpy(free_slot->tx_buf, tx_buf, tx_len);
			free_slot->state = SLOT_ACTIVE;
			found = free_slot;
		}
	}
	return found;
}

static void slot_release(struct lora_pending_slot *slot)
{
	K_SPINLOCK(&slot_lock)
	{
		slot->state = SLOT_FREE;
	}
}

/* ACTIVE -> DONE. Returns true for the single caller that wins the transition. */
static bool slot_try_finish(struct lora_pending_slot *slot, int status, bool via_work)
{
	bool won = false;

	K_SPINLOCK(&slot_lock)
	{
		if (slot->state == SLOT_ACTIVE) {
			slot->state = SLOT_DONE;
			slot->response_status = status;
			slot->needs_deliver = via_work;
			won = true;
		}
	}
	return won;
}

/* Deliver the result of a DONE slot, then free it. The blocking caller frees
 * the slot itself after reading the status; the slot must not be touched
 * after k_sem_give() in that case.
 */
static void slot_deliver(struct lora_pending_slot *slot)
{
	struct k_sem *sem = slot->done_sem;

	if (sem != NULL) {
		k_sem_give(sem);
		return;
	}
	if (slot->cb != NULL) {
		slot->cb(slot->node_id, slot->response_status, slot->user_data);
	}
	slot_release(slot);
}

/* Record the completion (ACK / RESP) and let the workqueue deliver it now
 * instead of waiting for the retry timer.
 */
static void slot_signal(struct lora_pending_slot *slot, int status)
{
	if (slot_try_finish(slot, status, true)) {
		k_work_reschedule_for_queue(&pending_workq, &slot->retry_work, K_NO_WAIT);
	}
}

/* Arm the retry timer unless the slot already completed (its delivery is then
 * queued). Doing the check and the schedule under slot_lock orders this against
 * slot_try_finish(): a completion reschedule can never be overridden.
 */
static void slot_arm_timer(struct lora_pending_slot *slot, int32_t timeout_ms)
{
	K_SPINLOCK(&slot_lock)
	{
		if (slot->state == SLOT_ACTIVE) {
			k_work_reschedule_for_queue(&pending_workq, &slot->retry_work,
						    K_MSEC(timeout_ms));
		}
	}
}

/* --------------------------------------------------------------------------
 * Calculate timeout with SF scaling and ±30% randomization
 * -------------------------------------------------------------------------- */

static int32_t calc_timeout_ms(int32_t base, int retries_left, int max_retries)
{
	int sf = CONFIG_LORA_RADIO_DEFAULT_SF;
	int factor = sf_timeout_factor(sf);
	int32_t timeout = (base * factor) / 10;

	/* Exponential backoff: 1x, 2x, 4x */
	int backoff_shift = max_retries - retries_left;
	timeout = timeout << backoff_shift;

	/* ±30% randomization */
	int32_t jitter = (timeout * 3) / 10;
	int32_t random_offset = (int32_t)(sys_rand32_get() % (uint32_t)(jitter * 2 + 1)) - jitter;

	return timeout + random_offset;
}

/* --------------------------------------------------------------------------
 * Re-encode the stored frame with a fresh seq.
 *
 * A retransmission is a new frame: the nonce is derived from (node, dir, seq),
 * so reusing the seq would reuse the AES-GCM nonce, and the receiver's replay
 * protection would drop the frame anyway. The header is cleartext and the
 * payload is recovered by decoding the stored frame, so no plaintext copy is
 * kept. Only called from the (single) pending workqueue thread.
 * -------------------------------------------------------------------------- */

static int slot_reencode(struct lora_pending_slot *slot)
{
	static uint8_t payload[LORA_MAX_PACKET_SF7];
	static uint8_t new_buf[LORA_MAX_PACKET_SF7];
	struct lora_session *s = lora_session_get(slot->node_id);
	struct lora_l2_header hdr;
	size_t payload_len;
	size_t new_len;
	uint32_t seq;
	int ret;

	if (s == NULL) {
		return -ENOENT;
	}
	ret = lora_packet_decode(slot->tx_buf, slot->tx_len, s->session_key, &hdr, payload,
				 sizeof(payload), &payload_len);
	if (ret < 0) {
		return ret;
	}
	ret = lora_session_next_tx_seq(&seq);
	if (ret < 0) {
		return ret;
	}
	lora_hdr_seq_set(&hdr, seq);
	ret = lora_packet_encode(&hdr, payload, payload_len, s->session_key, new_buf,
				 sizeof(new_buf), &new_len);
	if (ret < 0) {
		return ret;
	}

	memcpy(slot->tx_buf, new_buf, new_len);
	slot->tx_len = (uint8_t)new_len;
	/* Remember every copy: an ACK of an earlier copy must still match */
	K_SPINLOCK(&slot_lock)
	{
		if (slot->n_seqs < SENT_SEQS_MAX) {
			slot->sent_seqs[slot->n_seqs++] = seq;
		}
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * Retry work handler
 * -------------------------------------------------------------------------- */

static void retry_work_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lora_pending_slot *slot = CONTAINER_OF(dwork, struct lora_pending_slot, retry_work);
	enum { ACT_NONE, ACT_DELIVER, ACT_RETRY } act = ACT_NONE;
	uint16_t node_id;

	K_SPINLOCK(&slot_lock)
	{
		node_id = slot->node_id;
		if (slot->state == SLOT_DONE && slot->needs_deliver) {
			/* ACK/RESP matched on the RX thread */
			slot->needs_deliver = false;
			act = ACT_DELIVER;
		} else if (slot->state == SLOT_ACTIVE) {
			if (slot->retries_left > 0) {
				slot->retries_left--;
				act = ACT_RETRY;
			} else {
				slot->state = SLOT_DONE;
				slot->response_status = -ETIMEDOUT;
				act = ACT_DELIVER;
			}
		}
	}

	if (act == ACT_DELIVER) {
		if (slot->response_status == -ETIMEDOUT) {
			LOG_ERR("retries exhausted for node 0x%04x", node_id);
		}
		slot_deliver(slot);
		return;
	}
	if (act != ACT_RETRY) {
		return;
	}

	LOG_WRN("retrying frame for node 0x%04x (%d left)", node_id, slot->retries_left);

	int ret = slot_reencode(slot);

	if (ret == 0) {
		ret = lora_radio_send(slot->tx_buf, slot->tx_len);
	}
	if (ret < 0) {
		LOG_ERR("retransmit to node 0x%04x failed: %d", node_id, ret);
		/* If an ACK completed the slot meanwhile, its delivery is queued */
		if (slot_try_finish(slot, ret, false)) {
			slot_deliver(slot);
		}
		return;
	}

	slot_arm_timer(slot, calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
					     CONFIG_LORA_RADIO_RETRY_MAX));
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void lora_pending_init(void)
{
	(void)memset(pending_slots, 0, sizeof(pending_slots));
	k_work_queue_start(&pending_workq, pending_wq_stack, sizeof(pending_wq_stack),
			   CONFIG_LORA_RADIO_RX_THREAD_PRIORITY + 1, NULL);

	for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
		k_work_init_delayable(&pending_slots[i].retry_work, retry_work_fn);
	}
}

int lora_pending_send(uint16_t node_id, uint32_t seq_num, uint8_t frame_type, const uint8_t *tx_buf,
		      uint8_t tx_len)
{
	struct k_sem done_sem;
	int err;

	k_sem_init(&done_sem, 0, 1);
	struct lora_pending_slot *slot =
		slot_claim(node_id, seq_num, frame_type, tx_buf, tx_len,
			   CONFIG_LORA_RADIO_RETRY_TIMEOUT_BASE_MS, &done_sem, NULL, NULL, &err);
	if (slot == NULL) {
		return err; /* -EBUSY: request already pending for this node */
	}

	int ret = lora_radio_send(slot->tx_buf, slot->tx_len);

	if (ret < 0) {
		if (slot_try_finish(slot, ret, false)) {
			slot_release(slot);
			return ret;
		}
		/* An ACK raced the failed send: its delivery is queued, wait for it */
	} else {
		slot_arm_timer(slot, calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
						     CONFIG_LORA_RADIO_RETRY_MAX));
	}

	/* Block until ACK or timeout */
	k_sem_take(&done_sem, K_FOREVER);
	ret = slot->response_status;
	slot_release(slot);

	return ret;
}

int lora_pending_send_async(uint16_t node_id, uint32_t seq_num, uint8_t frame_type,
			    const uint8_t *tx_buf, uint8_t tx_len, int32_t timeout_ms,
			    lora_pending_cb_t cb, void *user_data)
{
	int err;
	struct lora_pending_slot *slot = slot_claim(node_id, seq_num, frame_type, tx_buf, tx_len,
						    timeout_ms, NULL, cb, user_data, &err);

	if (slot == NULL) {
		return err;
	}

	int ret = lora_radio_send(slot->tx_buf, slot->tx_len);

	if (ret < 0) {
		if (slot_try_finish(slot, ret, false)) {
			slot_release(slot);
			return ret;
		}
		return 0; /* completed by an ACK meanwhile, delivery is queued */
	}

	slot_arm_timer(slot, calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
					     CONFIG_LORA_RADIO_RETRY_MAX));
	return 0;
}

void lora_pending_ack_match(uint16_t node_id, uint32_t seq_num, int status)
{
	struct lora_pending_slot *match = NULL;

	K_SPINLOCK(&slot_lock)
	{
		for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX && match == NULL; i++) {
			struct lora_pending_slot *p = &pending_slots[i];

			if (p->state != SLOT_ACTIVE || p->node_id != node_id) {
				continue;
			}
			/* Any copy sent for this request matches, not just the latest */
			for (uint8_t n = 0; n < p->n_seqs; n++) {
				if (p->sent_seqs[n] == seq_num) {
					match = p;
					break;
				}
			}
		}
	}
	if (match != NULL) {
		slot_signal(match, status);
	}
}

void lora_pending_complete(uint16_t node_id, uint8_t frame_type, int status)
{
	struct lora_pending_slot *match = NULL;

	K_SPINLOCK(&slot_lock)
	{
		for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
			struct lora_pending_slot *p = &pending_slots[i];

			if (p->state == SLOT_ACTIVE && p->node_id == node_id &&
			    p->frame_type == frame_type) {
				match = p;
				break;
			}
		}
	}
	if (match != NULL) {
		slot_signal(match, status);
	}
}

void lora_pending_cancel(uint16_t node_id)
{
	struct lora_pending_slot *match = NULL;

	K_SPINLOCK(&slot_lock)
	{
		for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
			if (pending_slots[i].state == SLOT_ACTIVE &&
			    pending_slots[i].node_id == node_id) {
				match = &pending_slots[i];
				break;
			}
		}
	}
	if (match == NULL) {
		return;
	}
	/* If an ACK completes the slot first, its queued delivery wins */
	if (slot_try_finish(match, -ECANCELED, false)) {
		k_work_cancel_delayable(&match->retry_work);
		slot_deliver(match);
	}
}
