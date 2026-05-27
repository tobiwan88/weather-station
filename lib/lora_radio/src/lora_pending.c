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
#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_pending.h>

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
 * Pending request slot — one per session
 * -------------------------------------------------------------------------- */

struct lora_pending_slot {
	struct k_work_delayable retry_work;
	struct k_sem *done_sem; /* NULL for async, non-NULL for blocking */
	uint16_t node_id;
	uint16_t seq_num;
	uint8_t frame_type;
	uint8_t retries_left;
	int32_t base_timeout_ms;
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	bool active;
	bool response_received;
	int response_status;
};

static struct lora_pending_slot pending_slots[CONFIG_LORA_RADIO_SESSION_MAX];
struct k_work_q pending_workq;
static K_THREAD_STACK_DEFINE(pending_wq_stack, 1024);

/* --------------------------------------------------------------------------
 * Find slot by node_id (returns NULL if not found)
 * -------------------------------------------------------------------------- */

static struct lora_pending_slot *slot_find(uint16_t node_id)
{
	for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
		if (pending_slots[i].active && pending_slots[i].node_id == node_id) {
			return &pending_slots[i];
		}
	}
	return NULL;
}

/* --------------------------------------------------------------------------
 * Find a free slot
 * -------------------------------------------------------------------------- */

static struct lora_pending_slot *slot_alloc(void)
{
	for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
		if (!pending_slots[i].active) {
			return &pending_slots[i];
		}
	}
	return NULL;
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
 * Retry work handler
 * -------------------------------------------------------------------------- */

static void retry_work_fn(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lora_pending_slot *slot = CONTAINER_OF(dwork, struct lora_pending_slot, retry_work);

	if (!slot->active) {
		return;
	}

	if (slot->response_received) {
		/* ACK received — signal completion */
		if (slot->done_sem) {
			k_sem_give(slot->done_sem);
		} else {
			/* Async: publish result on zbus channel */
			struct lora_rpc_result_event evt = {
				.node_id = slot->node_id,
				.cmd_id = 0,
				.status = slot->response_status,
				.resp_len = 0,
			};
			(void)zbus_chan_pub(&lora_rpc_result_chan, &evt, K_NO_WAIT);
		}
		slot->active = false;
		return;
	}

	if (slot->retries_left > 0) {
		slot->retries_left--;
		LOG_WRN("retrying frame for node 0x%04x (%d left)", slot->node_id,
			slot->retries_left);

		lora_send(lora_radio_dev, slot->tx_buf, slot->tx_len);

		int32_t timeout = calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
						  CONFIG_LORA_RADIO_RETRY_MAX);
		k_work_reschedule_for_queue(&pending_workq, &slot->retry_work, K_MSEC(timeout));
	} else {
		/* Retries exhausted */
		LOG_ERR("retries exhausted for node 0x%04x", slot->node_id);
		slot->response_status = -ETIMEDOUT;
		slot->response_received = true;

		if (slot->done_sem) {
			k_sem_give(slot->done_sem);
		} else {
			struct lora_rpc_result_event evt = {
				.node_id = slot->node_id,
				.cmd_id = 0,
				.status = -ETIMEDOUT,
				.resp_len = 0,
			};
			(void)zbus_chan_pub(&lora_rpc_result_chan, &evt, K_NO_WAIT);
		}
		slot->active = false;
	}
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

int lora_pending_send(uint16_t node_id, uint16_t seq_num, uint8_t frame_type, const uint8_t *tx_buf,
		      uint8_t tx_len)
{
	struct lora_pending_slot *slot = slot_find(node_id);
	if (slot) {
		/* Already have a pending request for this node — reject */
		return -EBUSY;
	}

	slot = slot_alloc();
	if (!slot) {
		return -ENOMEM;
	}

	struct k_sem done_sem;

	k_sem_init(&done_sem, 0, 1);
	slot->done_sem = &done_sem;
	slot->node_id = node_id;
	slot->seq_num = seq_num;
	slot->frame_type = frame_type;
	slot->retries_left = CONFIG_LORA_RADIO_RETRY_MAX;
	slot->base_timeout_ms = CONFIG_LORA_RADIO_RETRY_TIMEOUT_BASE_MS;
	slot->tx_len = tx_len;
	slot->response_received = false;
	slot->response_status = 0;
	slot->active = true;
	memcpy(slot->tx_buf, tx_buf, tx_len);

	/* Send the frame */
	int ret = lora_send(lora_radio_dev, (uint8_t *)tx_buf, tx_len);
	if (ret < 0) {
		slot->active = false;
		return ret;
	}

	/* Schedule first retry timer */
	int32_t timeout = calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
					  CONFIG_LORA_RADIO_RETRY_MAX);
	k_work_schedule_for_queue(&pending_workq, &slot->retry_work, K_MSEC(timeout));

	/* Block until ACK or timeout */
	k_sem_take(&done_sem, K_FOREVER);
	slot->done_sem = NULL;

	return slot->response_status;
}

int lora_pending_send_async(uint16_t node_id, uint16_t seq_num, uint8_t frame_type,
			    const uint8_t *tx_buf, uint8_t tx_len)
{
	struct lora_pending_slot *slot = slot_find(node_id);
	if (slot) {
		return -EBUSY;
	}

	slot = slot_alloc();
	if (!slot) {
		return -ENOMEM;
	}

	slot->done_sem = NULL;
	slot->node_id = node_id;
	slot->seq_num = seq_num;
	slot->frame_type = frame_type;
	slot->retries_left = CONFIG_LORA_RADIO_RETRY_MAX;
	slot->base_timeout_ms = CONFIG_LORA_RADIO_RETRY_TIMEOUT_BASE_MS;
	slot->tx_len = tx_len;
	slot->response_received = false;
	slot->response_status = 0;
	slot->active = true;
	memcpy(slot->tx_buf, tx_buf, tx_len);

	int ret = lora_send(lora_radio_dev, (uint8_t *)tx_buf, tx_len);
	if (ret < 0) {
		slot->active = false;
		return ret;
	}

	int32_t timeout = calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
					  CONFIG_LORA_RADIO_RETRY_MAX);
	k_work_schedule_for_queue(&pending_workq, &slot->retry_work, K_MSEC(timeout));

	return 0;
}

void lora_pending_ack_match(uint16_t node_id, uint16_t seq_num, int status)
{
	struct lora_pending_slot *slot = slot_find(node_id);
	if (!slot) {
		return;
	}

	if (slot->seq_num != seq_num) {
		return;
	}

	slot->response_received = true;
	slot->response_status = status;

	/* Cancel the pending retry timer — the work handler will signal completion */
}

void lora_pending_cancel(uint16_t node_id)
{
	struct lora_pending_slot *slot = slot_find(node_id);
	if (!slot) {
		return;
	}

	k_work_cancel_delayable(&slot->retry_work);
	slot->active = false;

	if (slot->done_sem) {
		slot->response_status = -ECANCELED;
		slot->response_received = true;
		k_sem_give(slot->done_sem);
	}
}
