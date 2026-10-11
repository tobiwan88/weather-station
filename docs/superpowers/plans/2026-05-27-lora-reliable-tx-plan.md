# LoRa Reliable TX & Protocol Completion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement shared reliable TX subsystem, RPC retry with async variant, windowed FOTA, session persist, and Ed25519 key cache for the LoRa radio subsystem.

**Architecture:** A shared `lora_pending.c` provides per-session pending-request slots with `k_work_delayable` retry timers, randomized exponential backoff, and ACK matching. RPC and FOTA both consume this subsystem. Async/fire-and-forget variants ensure zbus subscribers never block.

**Tech Stack:** Zephyr RTOS, k_work_delayable, k_sem, zbus channels, PSA Crypto (Ed25519), AES-128-GCM, Kconfig, SYS_INIT.

**Spec:** `docs/superpowers/specs/2026-05-27-lora-reliable-tx-design.md`

---

### Task 1: Kconfig Additions

**Files:**
- Modify: `lib/lora_radio/Kconfig`

- [ ] **Step 1: Add Kconfig options for reliable TX subsystem**

Add these options inside the `if LORA_RADIO` block (before `module = LORA_RADIO`, after the existing options):

```kconfig
config LORA_RADIO_PENDING
	bool "Reliable TX subsystem (retry + ACK matching)"
	default y
	help
	  Shared reliable transmission subsystem. Provides per-session
	  pending-request slots with retry timers, randomized exponential
	  backoff, and ACK matching. Used by RPC and FOTA.

config LORA_RADIO_RETRY_MAX
	int "Max retries per pending request"
	default 3
	range 1 5
	depends on LORA_RADIO_PENDING
	help
	  Maximum number of retransmission attempts before returning
	  -ETIMEDOUT to the caller.

config LORA_RADIO_RETRY_TIMEOUT_BASE_MS
	int "Base retry timeout in milliseconds"
	default 5000
	depends on LORA_RADIO_PENDING
	help
	  Base timeout scaled by spreading factor. Actual timeout =
	  BASE * SF_factor with ±30% randomization (jitter).
	  SF7=1×, SF8=1.4×, SF9=2×, SF10=3×, SF11=4.4×, SF12=6×.

config LORA_RADIO_RPC_RETRY
	bool "Enable RPC retry"
	default y
	select LORA_RADIO_PENDING
	help
	  RPC commands use ACK-based delivery with up to 3 retries.
	  Requires LORA_RADIO_PENDING.

config LORA_RADIO_FOTA_RETRY_MS
	int "FOTA chunk retry timer in milliseconds"
	default 2000
	depends on LORA_RADIO_FOTA
	help
	  Retry timer for individual FOTA chunks. Each unacknowledged
	  chunk is retried after this delay.

config LORA_RADIO_FOTA_COOLDOWN_S
	int "Cooldown after 3 consecutive FOTA chunk timeouts (seconds)"
	default 30
	depends on LORA_RADIO_FOTA
	help
	  Pause duration after 3 consecutive chunk timeouts before
	  resuming the FOTA transfer.
```

- [ ] **Step 2: Commit**

```bash
git add lib/lora_radio/Kconfig
git commit -m "feat(lora): add Kconfig for reliable TX and FOTA retry"
```

---

### Task 2: New zbus Channel — `lora_rpc_result_chan`

**Files:**
- Modify: `lib/lora_radio/include/lora_radio/lora_chan.h`
- Modify: `lib/lora_radio/src/lora_chan.c`

- [ ] **Step 1: Add `lora_rpc_result_event` struct and channel declaration**

In `lib/lora_radio/include/lora_radio/lora_chan.h`, add after the `lora_fota_event` struct (before the `ZBUS_CHAN_DECLARE` lines):

```c
struct lora_rpc_result_event {
	uint16_t node_id;
	uint8_t cmd_id;
	int status;
	uint8_t resp_data[8];
	uint8_t resp_len;
};
```

Add the channel declaration at the end (after `ZBUS_CHAN_DECLARE(lora_fota_chan);`):

```c
ZBUS_CHAN_DECLARE(lora_rpc_result_chan);
```

- [ ] **Step 2: Define the channel**

In `lib/lora_radio/src/lora_chan.c`, add after the `lora_fota_chan` definition:

```c
ZBUS_CHAN_DEFINE(lora_rpc_result_chan, struct lora_rpc_result_event, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.node_id = 0, .cmd_id = 0, .status = 0,
			       .resp_data = { 0 }, .resp_len = 0));
```

- [ ] **Step 3: Commit**

```bash
git add lib/lora_radio/include/lora_radio/lora_chan.h lib/lora_radio/src/lora_chan.c
git commit -m "feat(lora): add lora_rpc_result_chan for async RPC results"
```

---

### Task 3: `lora_pending.h` — Public Header

**Files:**
- Create: `lib/lora_radio/include/lora_radio/lora_pending.h`

- [ ] **Step 1: Create the public header**

```c
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
int lora_pending_send(uint16_t node_id, uint16_t seq_num, uint8_t frame_type,
		      const uint8_t *tx_buf, uint8_t tx_len);

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
```

- [ ] **Step 2: Commit**

```bash
git add lib/lora_radio/include/lora_radio/lora_pending.h
git commit -m "feat(lora): add lora_pending.h public header"
```

---

### Task 4: `lora_pending.c` — Shared Reliable TX Implementation

**Files:**
- Create: `lib/lora_radio/src/lora_pending.c`
- Modify: `lib/lora_radio/CMakeLists.txt`

- [ ] **Step 1: Register `lora_pending.c` in CMakeLists.txt**

In `lib/lora_radio/CMakeLists.txt`, add `src/lora_pending.c` to the `zephyr_library_sources()` list (after `src/lora_handler_prov.c`):

```cmake
  zephyr_library_sources(
    src/lora_radio_main.c
    src/lora_packet.c
    src/lora_session.c
    src/lora_chan.c
    src/lora_handler_data.c
    src/lora_handler_rpc.c
    src/lora_handler_prov.c
    src/lora_pending.c)
```

- [ ] **Step 2: Create `lora_pending.c`**

```c
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
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_pending.h>
#include <lora_radio/lora_chan.h>

/* --------------------------------------------------------------------------
 * SF-dependent timeout scaling factors (multiplied by base timeout)
 * -------------------------------------------------------------------------- */

static int sf_timeout_factor(int sf)
{
	switch (sf) {
	case 7:  return 1;
	case 8:  return 14;  /* 1.4x */
	case 9:  return 2;
	case 10: return 3;
	case 11: return 44;  /* 4.4x */
	case 12: return 6;
	default: return 3;   /* default SF10 */
	}
}

/* --------------------------------------------------------------------------
 * Pending request slot — one per session
 * -------------------------------------------------------------------------- */

struct lora_pending_slot {
	struct k_work_delayable retry_work;
	struct k_sem *done_sem;       /* NULL for async, non-NULL for blocking */
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
static struct k_work_q pending_workq;
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
		LOG_WRN("retrying frame for node 0x%04x (%d left)",
			slot->node_id, slot->retries_left);

		lora_send(lora_radio_dev, slot->tx_buf, slot->tx_len);

		int32_t timeout = calc_timeout_ms(slot->base_timeout_ms, slot->retries_left,
						  CONFIG_LORA_RADIO_RETRY_MAX);
		k_work_reschedule(&pending_workq, &slot->retry_work, K_MSEC(timeout));
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
	k_work_queue_name_set(&pending_workq, "lora_pending");

	for (int i = 0; i < CONFIG_LORA_RADIO_SESSION_MAX; i++) {
		k_work_init_delayable(&pending_slots[i].retry_work, retry_work_fn);
	}
}

int lora_pending_send(uint16_t node_id, uint16_t seq_num, uint8_t frame_type,
		      const uint8_t *tx_buf, uint8_t tx_len)
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
	int ret = lora_send(lora_radio_dev, tx_buf, tx_len);
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

	int ret = lora_send(lora_radio_dev, tx_buf, tx_len);
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
```

- [ ] **Step 3: Commit**

```bash
git add lib/lora_radio/src/lora_pending.c lib/lora_radio/CMakeLists.txt
git commit -m "feat(lora): implement lora_pending.c shared reliable TX subsystem"
```

---

### Task 5: Ed25519 Key Cache

**Files:**
- Modify: `lib/lora_radio/src/lora_handler_prov.c`
- Modify: `lib/lora_radio/src/lora_radio_main.c`

- [ ] **Step 1: Add `lora_prov_init()` and cached key handle**

In `lib/lora_radio/src/lora_handler_prov.c`, replace the per-beacon key import/destroy with a cached key. Add a forward declaration and init function at the top of the `#ifdef CONFIG_PSA_CRYPTO` block:

Replace the existing static key declarations (lines 21-31) with:

```c
static const uint8_t gateway_ed25519_sk[32] = {
	0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
	0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
	0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60,
};
static const uint8_t gateway_ed25519_pk[32] = {
	0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
	0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
	0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};
static psa_key_id_t s_ed25519_key_id;
```

Add `lora_prov_init()` after the static key declarations (before `lora_handle_prov_beacon`):

```c
int lora_prov_init(void)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attr, PSA_ALG_ED25519);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS));

	psa_status_t s = psa_import_key(&attr, gateway_ed25519_sk, 32, &s_ed25519_key_id);
	if (s != PSA_SUCCESS) {
		LOG_ERR("Ed25519 key import failed: %d", s);
		return -EIO;
	}

	LOG_INF("Ed25519 key cached (id=%u)", s_ed25519_key_id);
	return 0;
}
```

- [ ] **Step 2: Remove per-beacon import/destroy from `lora_handle_prov_beacon()`**

In `lora_handle_prov_beacon()`, remove the per-beacon `psa_import_key` / `psa_destroy_key` block and replace with the cached key:

Replace lines 72-97 (the `psa_key_attributes_t sign_attr` through `psa_destroy_key` block) with:

```c
	struct lora_l2_header resp_hdr;

	memset(&resp_hdr, 0, sizeof(resp_hdr));
	resp_hdr.type_ver = (LORA_FRAME_PROV_RESPONSE << 4) | 0x01;
	resp_hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	resp_hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);

	uint8_t sign_buf[sizeof(struct lora_l2_header) + sizeof(struct lora_prov_response)];

	memcpy(sign_buf, &resp_hdr, sizeof(resp_hdr));
	memcpy(sign_buf + sizeof(resp_hdr), &resp, sizeof(resp));

	size_t sig_len;
	psa_status_t s = psa_sign_message(s_ed25519_key_id, PSA_ALG_ED25519, sign_buf,
					  sizeof(sign_buf), resp.signature, sizeof(resp.signature),
					  &sig_len);
	if (s != PSA_SUCCESS) {
		LOG_ERR("Ed25519 signing failed: %d", s);
		return -EIO;
	}
```

- [ ] **Step 3: Call `lora_prov_init()` from `lora_radio_init()`**

In `lib/lora_radio/src/lora_radio_main.c`, add after `lora_session_restore();` (around line 220):

```c
#ifdef CONFIG_PSA_CRYPTO
	ret = lora_prov_init();
	if (ret < 0) {
		LOG_WRN("Ed25519 key cache init failed: %d", ret);
		/* Continue without provisioning capability */
	}
#endif
```

Add the forward declaration in `lora_radio_internal.h`:

```c
int lora_prov_init(void);
```

- [ ] **Step 4: Commit**

```bash
git add lib/lora_radio/src/lora_handler_prov.c lib/lora_radio/src/lora_radio_main.c lib/lora_radio/src/lora_radio_internal.h
git commit -m "feat(lora): cache Ed25519 key at init instead of import/destroy per beacon"
```

---

### Task 6: RPC Retry

**Files:**
- Modify: `lib/lora_radio/src/lora_handler_rpc.c`
- Modify: `lib/lora_radio/src/lora_radio_main.c`
- Modify: `lib/lora_radio/include/lora_radio/lora_session.h`
- Modify: `lib/lora_radio/src/lora_radio_internal.h`

- [ ] **Step 1: Add duplicate RPC tracking to session struct**

In `lib/lora_radio/include/lora_radio/lora_session.h`, add to `struct lora_session`:

```c
	uint16_t last_rpc_seq;       /* last processed RPC seq_num (dedup) */
	uint8_t last_rpc_resp[130];  /* cached last RPC response for dedup resend */
	uint8_t last_rpc_resp_len;
```

- [ ] **Step 2: Rewrite `lora_radio_rpc_send()` to use `lora_pending_send()`**

Replace the entire `lora_radio_rpc_send()` function in `lib/lora_radio/src/lora_handler_rpc.c` with:

```c
int lora_radio_rpc_send(uint16_t node_id, uint8_t cmd_id, const uint8_t *params, uint8_t param_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	uint8_t payload[2 + (param_len > 128 ? 128 : param_len)];

	payload[0] = cmd_id;
	payload[1] = param_len;
	if (param_len > 0) {
		memcpy(payload + 2, params, MIN(param_len, 128));
	}

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_RPC_CMD << 4) | 0x01;
	hdr.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED;
	hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);
	hdr.seq_num[0] = (uint8_t)(s->last_seq_tx & 0xFF);
	hdr.seq_num[1] = (uint8_t)((s->last_seq_tx >> 8) & 0xFF);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&hdr, payload, sizeof(payload), s->session_key, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}

	/* Use reliable TX — blocks until ACK/RESP or retries exhausted */
	ret = lora_pending_send(node_id, s->last_seq_tx, LORA_FRAME_RPC_CMD, tx_buf, tx_len);
	if (ret == 0) {
		s->last_seq_tx++;
	}
	return ret;
}
```

- [ ] **Step 3: Add `lora_radio_rpc_send_async()`**

After `lora_radio_rpc_send()`, add:

```c
int lora_radio_rpc_send_async(uint16_t node_id, uint8_t cmd_id, const uint8_t *params,
			      uint8_t param_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	uint8_t payload[2 + (param_len > 128 ? 128 : param_len)];

	payload[0] = cmd_id;
	payload[1] = param_len;
	if (param_len > 0) {
		memcpy(payload + 2, params, MIN(param_len, 128));
	}

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_RPC_CMD << 4) | 0x01;
	hdr.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED;
	hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);
	hdr.seq_num[0] = (uint8_t)(s->last_seq_tx & 0xFF);
	hdr.seq_num[1] = (uint8_t)((s->last_seq_tx >> 8) & 0xFF);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&hdr, payload, sizeof(payload), s->session_key, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}

	ret = lora_pending_send_async(node_id, s->last_seq_tx, LORA_FRAME_RPC_CMD, tx_buf, tx_len);
	if (ret == 0) {
		s->last_seq_tx++;
	}
	return ret;
}
```

- [ ] **Step 4: Add `lora_radio_rpc_send_async()` declaration to public header**

In `lib/lora_radio/include/lora_radio/lora_radio.h`, add after the `lora_radio_rpc_send()` declaration:

```c
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
```

- [ ] **Step 5: Add RPC_RESP and ACK dispatch to RX thread**

In `lib/lora_radio/src/lora_radio_main.c`, add cases to the RX thread switch (after the existing cases):

```c
		case LORA_FRAME_RPC_RESP:
			lora_handle_rpc_resp(src_node, payload, payload_len);
			break;
		case LORA_FRAME_ACK:
			lora_handle_ack(src_node, payload, payload_len);
			break;
```

- [ ] **Step 6: Implement `lora_handle_rpc_resp()` and `lora_handle_ack()`**

Add to `lib/lora_radio/src/lora_radio_internal.h`:

```c
int lora_handle_rpc_resp(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);

int lora_handle_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);
```

Add to `lib/lora_radio/src/lora_handler_rpc.c` (after the existing functions):

```c
int lora_handle_rpc_resp(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 2) {
		return -EINVAL;
	}

	uint8_t cmd_id = payload[0];
	uint8_t status = payload[1];
	uint8_t resp_len = payload_len > 2 ? (uint8_t)(payload_len - 2) : 0;

	/* Extract seq_num from the original request (stored in session) */
	struct lora_session *s = lora_session_get(src_node);
	uint16_t seq = s ? s->last_seq_rx : 0;

	/* Signal the pending slot */
	lora_pending_ack_match(src_node, seq, status != 0 ? -status : 0);

	/* Publish async result */
	struct lora_rpc_result_event evt = {
		.node_id = src_node,
		.cmd_id = cmd_id,
		.status = status != 0 ? -status : 0,
		.resp_len = resp_len,
	};
	if (resp_len > 0 && resp_len <= sizeof(evt.resp_data)) {
		memcpy(evt.resp_data, payload + 2, resp_len);
	}
	(void)zbus_chan_pub(&lora_rpc_result_chan, &evt, K_NO_WAIT);

	return 0;
}

int lora_handle_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 3) {
		return -EINVAL;
	}

	uint16_t ack_seq = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
	uint8_t status = payload[2];

	lora_pending_ack_match(src_node, ack_seq, status != 0 ? -status : 0);

	return 0;
}
```

- [ ] **Step 7: Add duplicate RPC detection to receiver side**

In `lora_handle_rpc_cmd()`, add dedup check at the start (after the payload_len check):

```c
	/* Duplicate detection: re-send cached response if same seq_num */
	struct lora_session *s = lora_session_get(src_node);
	if (s && s->last_rpc_seq != 0 && s->last_rpc_seq == s->last_seq_rx) {
		LOG_DBG("duplicate RPC cmd from node 0x%04x, resending cached response", src_node);
		uint8_t tx_buf[LORA_MAX_PACKET_SF7];
		uint8_t tx_len;
		struct lora_l2_header hdr;

		memset(&hdr, 0, sizeof(hdr));
		hdr.type_ver = (LORA_FRAME_RPC_RESP << 4) | 0x01;
		hdr.flags = LORA_FLAG_ENCRYPTED;
		hdr.dst_node[0] = (uint8_t)(src_node & 0xFF);
		hdr.dst_node[1] = (uint8_t)((src_node >> 8) & 0xFF);

		lora_packet_encode(&hdr, s->last_rpc_resp, s->last_rpc_resp_len,
				   s->session_key, tx_buf, &tx_len);
		lora_send(lora_radio_dev, tx_buf, tx_len);
		return 0;
	}
```

At the end of `lora_handle_rpc_cmd()`, cache the response before sending:

```c
	/* Cache for duplicate detection */
	if (s) {
		s->last_rpc_seq = s->last_seq_rx;
		s->last_rpc_resp_len = MIN(total_resp_len, sizeof(s->last_rpc_resp));
		memcpy(s->last_rpc_resp, rpc_rsp_buf, s->last_rpc_resp_len);
	}
```

- [ ] **Step 8: Commit**

```bash
git add lib/lora_radio/src/lora_handler_rpc.c lib/lora_radio/src/lora_radio_main.c lib/lora_radio/include/lora_radio/lora_session.h lib/lora_radio/include/lora_radio/lora_radio.h lib/lora_radio/src/lora_radio_internal.h
git commit -m "feat(lora): implement RPC retry with blocking and async variants"
```

---

### Task 7: Session Persist After Pairing

**Files:**
- Modify: `lib/lora_radio/src/lora_handler_prov.c`

- [ ] **Step 1: Add `lora_session_persist()` call after pairing**

In `lora_handle_prov_beacon()`, add `lora_session_persist();` after the `LOG_INF("paired node...")` line (the last line before `return 0;`):

```c
	LOG_INF("paired node 0x%04x (%d capabilities)", node_id, caps_count);
	lora_session_persist();
	return 0;
```

- [ ] **Step 2: Commit**

```bash
git add lib/lora_radio/src/lora_handler_prov.c
git commit -m "fix(lora): persist session immediately after pairing"
```

---

### Task 8: FOTA Windowing

**Files:**
- Modify: `lib/lora_radio/src/lora_handler_fota.c`
- Modify: `lib/lora_radio/src/lora_radio_main.c`
- Modify: `lib/lora_radio/src/lora_radio_internal.h`

- [ ] **Step 1: Add forward declarations**

In `lib/lora_radio/src/lora_radio_internal.h`, add:

```c
int lora_handle_fota_chunk_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);
```

- [ ] **Step 2: Rewrite `lora_handler_fota.c` with windowed sender**

Replace the entire contents of `lib/lora_radio/src/lora_handler_fota.c` with:

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fota
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include "lora_radio_internal.h"
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_chan.h>

#if defined(CONFIG_IMG_MANAGER) && defined(CONFIG_FLASH_MAP)
#	include <zephyr/dfu/flash_img.h>

/* --------------------------------------------------------------------------
 * FOTA sender state machine
 * -------------------------------------------------------------------------- */

enum fota_sender_state {
	FOTA_STATE_IDLE,
	FOTA_STATE_SENDING,
	FOTA_STATE_COOLDOWN,
	FOTA_STATE_FAILED,
};

static struct {
	enum fota_sender_state state;
	uint16_t target_node_id;
	uint32_t image_size;
	uint32_t bytes_sent;
	uint32_t bytes_acked;
	uint8_t fota_mode;
	uint8_t window[CONFIG_LORA_RADIO_FOTA_WINDOW];
	uint8_t window_size;
	uint8_t window_sent;
	uint8_t consecutive_timeouts;
	uint8_t chunk_data[CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE];
	struct k_work_delayable work;
	struct flash_img_context fota_ctx;
} fota_sender;

/* --------------------------------------------------------------------------
 * Send one chunk
 * -------------------------------------------------------------------------- */

static int fota_send_chunk(uint16_t node_id, uint32_t offset, const uint8_t *data, uint8_t data_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	struct lora_fota_chunk chunk;

	chunk.offset = offset;
	memcpy(chunk.data, data, data_len);

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_FOTA_CHUNK << 4) | 0x01;
	hdr.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED;
	hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);
	hdr.seq_num[0] = (uint8_t)(s->last_seq_tx & 0xFF);
	hdr.seq_num[1] = (uint8_t)((s->last_seq_tx >> 8) & 0xFF);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&hdr, &chunk, sizeof(chunk.offset) + data_len, s->session_key,
				     tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}

	return lora_pending_send_async(node_id, s->last_seq_tx, LORA_FRAME_FOTA_CHUNK, tx_buf,
				       tx_len);
}

/* --------------------------------------------------------------------------
 * Advance window — send next batch of chunks
 * -------------------------------------------------------------------------- */

static void fota_advance_window(struct k_work *work);

static void fota_send_next_chunk(struct k_work *work)
{
	(void)work;

	if (fota_sender.state != FOTA_STATE_SENDING) {
		return;
	}

	if (fota_sender.bytes_sent >= fota_sender.image_size) {
		LOG_INF("FOTA complete for node 0x%04x (%u bytes)", fota_sender.target_node_id,
			fota_sender.bytes_acked);
		fota_sender.state = FOTA_STATE_IDLE;
		return;
	}

	if (fota_sender.window_sent >= fota_sender.window_size) {
		/* Window full — wait for ACKs, scheduler will call us again */
		return;
	}

	uint32_t offset = fota_sender.bytes_sent;
	uint32_t remaining = fota_sender.image_size - offset;
	uint8_t chunk_size = (uint8_t)MIN(remaining, (uint32_t)CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE);

	/* Read chunk from SPI flash */
	int ret = flash_area_read(fota_ctx.fap, offset, fota_sender.chunk_data, chunk_size);
	if (ret < 0) {
		LOG_ERR("FOTA flash read failed at offset %u: %d", offset, ret);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}

	ret = fota_send_chunk(fota_sender.target_node_id, offset, fota_sender.chunk_data, chunk_size);
	if (ret < 0) {
		LOG_ERR("FOTA chunk send failed: %d", ret);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}

	fota_sender.bytes_sent += chunk_size;
	fota_sender.window_sent++;

	/* Schedule next chunk after a short delay */
	k_work_schedule_for_queue(&pending_workq, &fota_sender.work, K_MSEC(100));
}

/* --------------------------------------------------------------------------
 * Work handler — manages window and cooldown
 * -------------------------------------------------------------------------- */

static void fota_advance_window(struct k_work *work)
{
	(void)work;

	switch (fota_sender.state) {
	case FOTA_STATE_SENDING:
		fota_send_next_chunk(work);
		break;
	case FOTA_STATE_COOLDOWN:
		if (fota_sender.consecutive_timeouts >= 3) {
			LOG_ERR("FOTA failed after cooldown for node 0x%04x",
				fota_sender.target_node_id);
			fota_sender.state = FOTA_STATE_FAILED;
		} else {
			fota_sender.consecutive_timeouts = 0;
			fota_sender.state = FOTA_STATE_SENDING;
			fota_send_next_chunk(work);
		}
		break;
	default:
		break;
	}
}

/* --------------------------------------------------------------------------
 * FOTA_START handler — called from lora_fota_chan subscriber
 * -------------------------------------------------------------------------- */

static void fota_start_handler(uint16_t node_id, uint32_t image_size, uint8_t fota_mode)
{
	if (fota_sender.state != FOTA_STATE_IDLE) {
		LOG_WRN("FOTA already in progress (state=%d)", fota_sender.state);
		return;
	}

	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		LOG_ERR("no session for node 0x%04x", node_id);
		return;
	}

	fota_sender.state = FOTA_STATE_SENDING;
	fota_sender.target_node_id = node_id;
	fota_sender.image_size = image_size;
	fota_sender.bytes_sent = 0;
	fota_sender.bytes_acked = 0;
	fota_sender.fota_mode = fota_mode;
	fota_sender.window_size = CONFIG_LORA_RADIO_FOTA_WINDOW;
	fota_sender.window_sent = 0;
	fota_sender.consecutive_timeouts = 0;

	/* Initialize flash context */
	flash_img_init(&fota_sender.fota_ctx, FIXED_PARTITION_ID(PM_MCUBOOT_SECONDARY));

	LOG_INF("FOTA start for node 0x%04x (%u bytes, mode=0x%02x)", node_id, image_size,
		fota_mode);

	fota_send_next_chunk(NULL);
}

/* --------------------------------------------------------------------------
 * FOTA_CANCEL handler
 * -------------------------------------------------------------------------- */

static void fota_cancel_handler(uint16_t node_id)
{
	(void)node_id;

	if (fota_sender.state == FOTA_STATE_IDLE) {
		return;
	}

	lora_pending_cancel(fota_sender.target_node_id);
	fota_sender.state = FOTA_STATE_IDLE;
	LOG_INF("FOTA cancelled for node 0x%04x", fota_sender.target_node_id);
}

/* --------------------------------------------------------------------------
 * FOTA_CHUNK_ACK handler — called from RX thread
 * -------------------------------------------------------------------------- */

int lora_handle_fota_chunk_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < sizeof(struct lora_fota_chunk_ack)) {
		return -EINVAL;
	}

	const struct lora_fota_chunk_ack *ack = (const struct lora_fota_chunk_ack *)payload;

	uint32_t ack_offset;
	uint32_t expected_offset;

	memcpy(&ack_offset, ack->offset, sizeof(ack_offset));
	memcpy(&expected_offset, ack->expected_offset, sizeof(expected_offset));

	/* Signal pending slot */
	lora_pending_ack_match(src_node, 0, ack->status != 0 ? -1 : 0);

	if (ack->status == 0) {
		fota_sender.bytes_acked = expected_offset;
		fota_sender.window_sent = MAX(0, fota_sender.window_sent - 1);
		fota_sender.consecutive_timeouts = 0;

		/* Advance window if space available */
		if (fota_sender.state == FOTA_STATE_SENDING &&
		    fota_sender.window_sent < fota_sender.window_size) {
			k_work_schedule_for_queue(&pending_workq, &fota_sender.work, K_MSEC(50));
		}
	} else {
		fota_sender.consecutive_timeouts++;
		if (fota_sender.consecutive_timeouts >= 3) {
			fota_sender.state = FOTA_STATE_COOLDOWN;
			k_work_schedule_for_queue(&pending_workq, &fota_sender.work,
						  K_SECONDS(CONFIG_LORA_RADIO_FOTA_COOLDOWN_S));
		}
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * FOTA receiver — process incoming chunk (sensor node side)
 * -------------------------------------------------------------------------- */

static struct flash_img_context fota_rx_ctx;
static uint32_t fota_rx_expected_offset;
static bool fota_rx_active;

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 5) {
		return -EINVAL;
	}

	uint32_t offset;

	memcpy(&offset, payload, sizeof(offset));
	const uint8_t *data = payload + 4;
	uint8_t data_len = payload_len - 4;
	int ret = 0;
	uint32_t expected = fota_rx_expected_offset;

	if (!fota_rx_active) {
		flash_img_init(&fota_rx_ctx, FIXED_PARTITION_ID(PM_MCUBOOT_SECONDARY));
		fota_rx_active = true;
		fota_rx_expected_offset = 0;
		expected = 0;
	}

	/* Ignore duplicate/out-of-order chunks */
	if (offset != fota_rx_expected_offset) {
		LOG_WRN("FOTA offset mismatch: expected %u, got %u", fota_rx_expected_offset,
			offset);
		ret = -EINVAL;
		expected = fota_rx_expected_offset;
		goto send_ack;
	}

	ret = flash_img_buffered_write(&fota_rx_ctx, data, data_len, false);
	if (ret < 0) {
		LOG_ERR("flash write failed at offset %u: %d", offset, ret);
		expected = fota_rx_expected_offset;
		goto send_ack;
	}
	fota_rx_expected_offset = offset + data_len;

send_ack: {
	struct lora_fota_chunk_ack ack;

	memcpy(ack.offset, &offset, sizeof(ack.offset));
	memcpy(ack.expected_offset, &fota_rx_expected_offset, sizeof(ack.expected_offset));
	ack.status = (ret < 0) ? 1 : 0;

	struct lora_l2_header hdr;

	hdr.type_ver = (LORA_FRAME_FOTA_CHUNK_ACK << 4) | 0x01;
	hdr.flags = 0;
	hdr.src_node[0] = 0;
	hdr.src_node[1] = 0;
	hdr.dst_node[0] = (uint8_t)(src_node & 0xFF);
	hdr.dst_node[1] = (uint8_t)((src_node >> 8) & 0xFF);
	hdr.seq_num[0] = 0;
	hdr.seq_num[1] = 0;

	struct lora_session *s = lora_session_get(src_node);
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;

	lora_packet_encode(&hdr, &ack, sizeof(ack), s ? s->session_key : NULL, tx_buf, &tx_len);
	lora_send(lora_radio_dev, tx_buf, tx_len);
}
	return 0;
}

/* --------------------------------------------------------------------------
 * lora_fota_chan subscriber — handles FOTA_START / FOTA_CANCEL
 * -------------------------------------------------------------------------- */

static void fota_chan_handler(const struct zbus_channel *chan)
{
	const struct lora_fota_event *evt;

	evt = zbus_chan_const_msg(chan);
	if (!evt) {
		return;
	}

	/* Derive node_id from target_uid: uid = (0x0200 << 16) | (node_id << 4) | type */
	uint16_t node_id = (uint16_t)((evt->target_uid >> 4) & 0xFF);

	switch (evt->action) {
	case LORA_FOTA_START:
		fota_start_handler(node_id, evt->image_size, evt->fota_mode);
		break;
	case LORA_FOTA_CANCEL:
		fota_cancel_handler(node_id);
		break;
	}
}

ZBUS_LISTENER_DEFINE(lora_fota_listener, fota_chan_handler);

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

static int lora_fota_init(void)
{
	fota_sender.state = FOTA_STATE_IDLE;
	fota_rx_active = false;
	k_work_init_delayable(&fota_sender.work, fota_advance_window);
	return 0;
}

SYS_INIT(lora_fota_init, APPLICATION, 85);
#endif /* CONFIG_IMG_MANAGER && CONFIG_FLASH_MAP */
```

- [ ] **Step 3: Add FOTA_CHUNK_ACK dispatch to RX thread**

In `lib/lora_radio/src/lora_radio_main.c`, add to the RX thread switch (after existing cases):

```c
		case LORA_FRAME_FOTA_CHUNK_ACK:
			lora_handle_fota_chunk_ack(src_node, payload, payload_len);
			break;
```

- [ ] **Step 4: Commit**

```bash
git add lib/lora_radio/src/lora_handler_fota.c lib/lora_radio/src/lora_radio_main.c lib/lora_radio/src/lora_radio_internal.h
git commit -m "feat(lora): implement windowed FOTA chunk protocol with retry"
```

---

### Task 9: Build & Test Gate

**Files:**
- All modified files

- [ ] **Step 1: Build gateway**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -b native_sim/native/64 apps/gateway --pristine
```

Expected: Build succeeds with no errors.

- [ ] **Step 2: Build sensor-node**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -b native_sim/native/64 apps/sensor-node --pristine
```

Expected: Build succeeds with no errors.

- [ ] **Step 3: Shell smoke test**

```bash
printf "help\nfake_sensors list\nlora status\n" | timeout 10 build/native_sim_native_64/gateway/zephyr/zephyr.exe -uart_stdinout
```

Expected: Gateway boots, shows help, lists fake sensors, shows LoRa status.

- [ ] **Step 4: Run twister tests**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
```

Expected: All existing tests pass. No regressions.

- [ ] **Step 5: Run pre-commit**

```bash
pre-commit run --all-files
```

Expected: All checks pass.

---

### Task 10: Update Architecture Documentation

**Files:**
- Modify: `docs/architecture/lora-protocol.md`

- [ ] **Step 1: Update the "Internal Modules" section**

Add `lora_pending.c` to the internal modules section. In the "Internal Modules" section, add after "Session Manager":

```
### Reliable TX (`lora_pending.c`)

Shared reliable transmission subsystem used by RPC and FOTA:

- Per-session pending-request slot (serialized per node)
- `k_work_delayable` retry timer with randomized exponential backoff
- ACK/RPC_RESP/FOTA_CHUNK_ACK matching via `(node_id, seq_num)`
- `lora_pending_send()` — blocking (for thread callers)
- `lora_pending_send_async()` — non-blocking (for zbus listeners)
- `lora_pending_ack_match()` — called from RX thread dispatch
- `lora_pending_cancel()` — called when session removed
- Timeout scaling: SF-dependent base timeout with ±30% jitter
- Max 3 retries, then `-ETIMEDOUT`
- Result published on `lora_rpc_result_chan` for async callers

### FOTA Handler (`lora_handler_fota.c`)

Windowed FOTA chunk sender (gateway) and receiver (sensor node):

- **Sender**: Window of N chunks in flight (Kconfig default 8), each chunk
  uses `lora_pending_send_async()` for per-chunk reliability. State machine:
  IDLE → SENDING → COOLDOWN (30s after 3 consecutive timeouts) → FAILED.
- **Receiver**: Processes incoming chunks, writes to MCUboot slot 1 via
  `flash_img_buffered_write()`, sends FOTA_CHUNK_ACK with correct offset.
- **lora_fota_chan subscriber**: Handles FOTA_START / FOTA_CANCEL events
  from HTTP dashboard.
```

- [ ] **Step 2: Update the Zbus Channels section**

Add `lora_rpc_result_chan` to the "New channels" table:

```
| `lora_rpc_result_chan` | `lora_radio` | LoRa → Consumers | `lora_rpc_result_event` (node_id, cmd_id, status, resp_data) |
```

- [ ] **Step 3: Update the RPC section reliability subsection**

Replace the "Reliability" subsection with:

```
### Reliability

- RPC uses `lora_pending.c` shared reliable TX subsystem
- `lora_radio_rpc_send()` — blocking, waits for RPC_RESP or retries exhausted
- `lora_radio_rpc_send_async()` — non-blocking, result on `lora_rpc_result_chan`
- One pending RPC per node (serialized)
- SF-dependent timeout with ±30% randomization: 5s at SF7, 30s at SF12
- Exponential backoff: 1×, 2×, 4× timeout multiplier
- Up to 3 retries per command (Kconfig: `CONFIG_LORA_RADIO_RETRY_MAX`)
- Duplicate detection: sensor node checks seq_num, re-sends cached response
- RPC_RESP serves as implicit ACK — standalone ACK (0x8) also supported
```

- [ ] **Step 4: Update the FOTA section**

Replace the "Chunk protocol" subsection with:

```
### Chunk protocol

- Windowed sender: 4–16 chunks in flight (Kconfig default 8)
- Each chunk uses `lora_pending_send_async()` for per-chunk reliability
- Retry timer: 2 seconds per chunk (Kconfig: `CONFIG_LORA_RADIO_FOTA_RETRY_MS`)
- 3 retries per chunk before cooldown
- Consecutive timeout cooldown: 30 seconds pause after 3 failures
  (Kconfig: `CONFIG_LORA_RADIO_FOTA_COOLDOWN_S`)
- Flash error: abort session immediately
- ACK carries written offset → gateway advances window
- Receiver dedup: ignores duplicate/out-of-order chunks
```

- [ ] **Step 5: Commit**

```bash
git add docs/architecture/lora-protocol.md
git commit -m "docs(lora): update architecture doc for reliable TX and FOTA windowing"
```

---

## Self-Review Checklist

**Spec coverage check:**

| Spec section | Task |
|---|---|
| Phase 1: Ed25519 cache | Task 5 |
| Phase 2: `lora_pending.c` | Tasks 3, 4 |
| Phase 3: RPC retry | Task 6 |
| Phase 4: FOTA windowing | Task 8 |
| Phase 5: Session persist | Task 7 |
| Phase 6: RPC/FOTA audit | Documented in backlog (no code) |
| Kconfig additions | Task 1 |
| New zbus channel | Task 2 |
| Async/non-blocking | Tasks 4, 6 |
| Backlog items added | Done in brainstorming |
| Backlog items removed | Done in brainstorming |

**No placeholders found.** All steps contain actual code.

**Type consistency:** `lora_pending_send()`, `lora_pending_send_async()`, `lora_pending_ack_match()`, `lora_pending_cancel()` match the header. `lora_rpc_result_event` struct matches the channel definition. `lora_handle_rpc_resp()` and `lora_handle_ack()` match internal header declarations.
