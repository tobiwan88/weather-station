/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_session
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <lora_radio/lora_session.h>

#define SESSION_MAX CONFIG_LORA_RADIO_SESSION_MAX
#define NODE_ID_MIN 0x0001U
#define NODE_ID_MAX 0x00FFU

#define TX_SEQ_NAME "txseq"

static struct lora_session sessions[CONFIG_LORA_RADIO_SESSION_MAX];
static uint8_t session_count;

/* Downlink counter shared by all nodes (the nonce also contains the node id) */
static struct lora_seq_counter tx_counter = {.next = 1, .reserved = 1};
static K_MUTEX_DEFINE(tx_counter_lock);

static int tx_seq_persist(uint32_t reserved)
{
	int ret = settings_save_one("lora/" TX_SEQ_NAME, &reserved, sizeof(reserved));

	if (ret == -ENOENT && IS_ENABLED(CONFIG_LORA_RADIO_ALLOW_VOLATILE_SEQ)) {
		/* No settings backend: the counter restarts at 1 every boot, which
		 * repeats nonces. Test targets only (see Kconfig).
		 */
		static bool warned;

		if (!warned) {
			warned = true;
			LOG_WRN("downlink seq not persisted (no settings backend), nonces "
				"repeat across reboots: test only");
		}
		return 0;
	}
	if (ret < 0) {
		LOG_ERR("persisting tx seq reservation failed: %d", ret);
	}
	return ret;
}

int lora_session_next_tx_seq(uint32_t *seq)
{
	k_mutex_lock(&tx_counter_lock, K_FOREVER);
	int ret = lora_seq_counter_next(&tx_counter, CONFIG_LORA_RADIO_SEQ_RESERVE, tx_seq_persist,
					seq);
	k_mutex_unlock(&tx_counter_lock);
	return ret;
}

void lora_session_init(void)
{
	lora_seq_counter_init(&tx_counter, 0);
	(void)memset(sessions, 0, sizeof(sessions));
	session_count = 0;
}

struct lora_session *lora_session_get(uint16_t node_id)
{
	for (uint8_t i = 0; i < session_count; i++) {
		if (sessions[i].node_id == node_id) {
			return &sessions[i];
		}
	}
	return NULL;
}

struct lora_session *lora_session_add(uint16_t node_id, const uint8_t key[16])
{
	if (session_count >= SESSION_MAX) {
		LOG_WRN("session table full (%d)", SESSION_MAX);
		return NULL;
	}
	struct lora_session *s = &sessions[session_count++];
	s->node_id = node_id;
	s->state = LORA_SESSION_PAIRED;
	memcpy(s->session_key, key, 16);
	s->last_seq_rx = 0;
	s->last_seq_tx = 0;
	s->replay_drops = 0;
	s->rx_seen = false;
	s->plain_warned = false;
	s->retry_count = 0;
	s->last_rx_ms = 0;
	return s;
}

bool lora_session_rx_accept(struct lora_session *s, uint32_t seq)
{
	if (s->rx_seen && seq <= s->last_seq_rx) {
		s->replay_drops++;
		LOG_DBG("node 0x%04x: drop seq %u <= last %u (drops=%u)", s->node_id, seq,
			s->last_seq_rx, s->replay_drops);
		return false;
	}
	s->rx_seen = true;
	s->last_seq_rx = seq;
	return true;
}

void lora_seq_counter_init(struct lora_seq_counter *c, uint32_t persisted_reserved)
{
	c->reserved = persisted_reserved;
	c->next = persisted_reserved != 0 ? persisted_reserved : 1;
	if (c->reserved < c->next) {
		c->reserved = c->next;
	}
}

int lora_seq_counter_next(struct lora_seq_counter *c, uint32_t reserve, lora_seq_persist_fn persist,
			  uint32_t *seq)
{
	if (c->next == UINT32_MAX) {
		return -EOVERFLOW;
	}
	if (c->next >= c->reserved) {
		/* Saturate instead of wrapping; the last value stays unused */
		uint32_t new_res =
			(c->reserved > UINT32_MAX - reserve) ? UINT32_MAX : c->reserved + reserve;
		int ret = persist(new_res);

		if (ret < 0) {
			return ret;
		}
		c->reserved = new_res;
	}
	*seq = c->next++;
	return 0;
}

void lora_session_remove(uint16_t node_id)
{
	for (uint8_t i = 0; i < session_count; i++) {
		if (sessions[i].node_id == node_id) {
			if (i < session_count - 1) {
				memmove(&sessions[i], &sessions[i + 1],
					(session_count - i - 1) * sizeof(struct lora_session));
			}
			session_count--;
			LOG_INF("unpaired node 0x%04x", node_id);
			return;
		}
	}
}

uint16_t lora_session_alloc_node_id(void)
{
	bool used[NODE_ID_MAX - NODE_ID_MIN + 1];
	(void)memset(used, 0, sizeof(used));
	for (uint8_t i = 0; i < session_count; i++) {
		uint16_t id = sessions[i].node_id;
		if (id >= NODE_ID_MIN && id <= NODE_ID_MAX) {
			used[id - NODE_ID_MIN] = true;
		}
	}
	for (uint16_t id = NODE_ID_MIN; id <= NODE_ID_MAX; id++) {
		if (!used[id - NODE_ID_MIN]) {
			return id;
		}
	}
	return 0;
}

static int lora_settings_set(const char *name, size_t len_rd, settings_read_cb read_cb,
			     void *cb_arg)
{
	if (!name) {
		return 0;
	}

	if (strcmp(name, TX_SEQ_NAME) == 0) {
		uint32_t reserved;

		if (len_rd != sizeof(reserved) ||
		    read_cb(cb_arg, &reserved, sizeof(reserved)) != sizeof(reserved)) {
			return -EINVAL;
		}
		lora_seq_counter_init(&tx_counter, reserved);
		return 0;
	}

	/* Parse "XXXX/key" — extract the 4-hex-digit node_id prefix */
	uint16_t node_id;
	int matched = sscanf(name, "%04hx/key", &node_id);
	if (matched != 1) {
		return 0;
	}

	if (len_rd < 16) {
		return -EINVAL;
	}

	uint8_t key[16];
	ssize_t ret = read_cb(cb_arg, key, sizeof(key));
	if (ret < 16) {
		return -EINVAL;
	}

	struct lora_session *s = lora_session_add(node_id, key);
	if (!s) {
		LOG_WRN("failed to restore session for node 0x%04x", node_id);
	}
	return 0;
}

static int lora_settings_export(int (*cb)(const char *name, const void *val, size_t len))
{
	char key[32];
	for (uint8_t i = 0; i < session_count; i++) {
		snprintk(key, sizeof(key), "lora/%04x/key", sessions[i].node_id);
		cb(key, sessions[i].session_key, 16);
	}
	cb("lora/" TX_SEQ_NAME, &tx_counter.reserved, sizeof(tx_counter.reserved));
	return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(lora_radio, "lora", NULL, lora_settings_set, NULL,
			       lora_settings_export);

void lora_session_persist(void)
{
	settings_save();
}

void lora_session_restore(void)
{
	settings_load_subtree("lora");
	LOG_INF("sessions restored (%d nodes)", session_count);
}
