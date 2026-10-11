/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_node
#define LOG_LEVEL       CONFIG_LORA_NODE_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <lora_node/lora_node.h>

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <psa/crypto.h>

#include <lora_proto/lora_packet.h>

/* --------------------------------------------------------------------------
 * Session state
 * -------------------------------------------------------------------------- */

static uint16_t session_node_id;
static uint8_t session_key[16];
static bool session_valid;

static int sf_to_enum(int sf)
{
	switch (sf) {
	case 7:
		return SF_7;
	case 8:
		return SF_8;
	case 9:
		return SF_9;
	case 10:
		return SF_10;
	case 11:
		return SF_11;
	case 12:
		return SF_12;
	default:
		return SF_10;
	}
}

static int bw_to_enum(int bw_khz)
{
	switch (bw_khz) {
	case 125:
		return BW_125_KHZ;
	case 250:
		return BW_250_KHZ;
	case 500:
		return BW_500_KHZ;
	default:
		return BW_125_KHZ;
	}
}

/* --------------------------------------------------------------------------
 * Settings handlers
 * -------------------------------------------------------------------------- */

struct session_data {
	uint16_t node_id;
	uint8_t key[16];
};

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (len != sizeof(struct session_data)) {
		return -EINVAL;
	}

	struct session_data data;
	int rc = read_cb(cb_arg, &data, sizeof(data));
	if (rc < 0) {
		return rc;
	}

	session_node_id = data.node_id;
	memcpy(session_key, data.key, sizeof(session_key));
	session_valid = true;
	return 0;
}

static struct settings_handler session_settings = {
	.name = CONFIG_LORA_NODE_SETTINGS_KEY,
	.h_set = settings_set,
};

/* --------------------------------------------------------------------------
 * Frame counter (AES-GCM nonce input), write-ahead reservation
 *
 * seq_next lives in RAM. seq_reserved_until is the persisted limit: counters
 * below it may be used; after a reboot the counter restarts at the limit.
 *
 * If a session key exists but no valid counter record could be loaded (first
 * boot with a dev key, settings wiped or corrupt), restarting at 1 would reuse
 * (key, seq) nonces of an earlier life of the same key. The counter then starts
 * at a random value in the upper half of the 32-bit space and is persisted
 * immediately (see seq_start_random()), so a settings wipe never rewinds it.
 *
 * tx_lock serializes seq allocation, encoding and lora_send(): the gateway
 * drops a frame whose seq is lower than the last accepted one as a replay, so
 * seqs must reach the air in order.
 * -------------------------------------------------------------------------- */

static K_MUTEX_DEFINE(tx_lock);
static uint32_t seq_next = 1;
static uint32_t seq_reserved_until;
static bool seq_loaded;

static int seq_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint32_t val;

	if (len != sizeof(val)) {
		return -EINVAL;
	}
	int rc = read_cb(cb_arg, &val, sizeof(val));
	if (rc < 0) {
		return rc;
	}
	if (val == 0 || val == UINT32_MAX) {
		return -EINVAL;
	}
	seq_next = val;
	seq_reserved_until = val;
	seq_loaded = true;
	return 0;
}

static struct settings_handler seq_settings = {
	.name = CONFIG_LORA_NODE_SEQ_SETTINGS_KEY,
	.h_set = seq_settings_set,
};

/* Persist a new block starting at seq_next. Caller holds tx_lock (or runs at init). */
static int seq_reserve(void)
{
	if (seq_next > UINT32_MAX - CONFIG_LORA_NODE_SEQ_RESERVE) {
		return -EOVERFLOW;
	}

	uint32_t limit = seq_next + CONFIG_LORA_NODE_SEQ_RESERVE;
	int ret = settings_save_one(CONFIG_LORA_NODE_SEQ_SETTINGS_KEY, &limit, sizeof(limit));
	if (ret < 0) {
		LOG_ERR("seq reservation failed: %d", ret);
		return -EIO;
	}
	seq_reserved_until = limit;
	return 0;
}

/* Start the counter at a random value in [0x80000000, 0xBFFFFFFF] and persist
 * the reservation now. Used when a key is present but no valid counter record is.
 */
static void seq_start_random(void)
{
	uint32_t r;
	int ret = sys_csrand_get(&r, sizeof(r));

	if (ret < 0) {
		LOG_ERR("CSPRNG failed (%d), cannot seed frame counter", ret);
		/* No safe start: mark the counter exhausted so encrypted TX is refused */
		seq_next = UINT32_MAX;
		seq_reserved_until = UINT32_MAX;
		return;
	}
	seq_next = 0x80000000U | (r & 0x3FFFFFFFU);
	seq_reserved_until = seq_next;
	LOG_WRN("no valid frame counter record, starting at 0x%08x", seq_next);
	(void)seq_reserve(); /* on failure TX retries the reservation and is refused */
}

/* --------------------------------------------------------------------------
 * Packet encoding
 * -------------------------------------------------------------------------- */

#define LORA_PROTOCOL_VERSION 0x1

BUILD_ASSERT(LORA_PROTO_MAX_READINGS(LORA_MAX_PACKET_SF12) >= 1,
	     "max payload too small for one reading");

static int encode_packet(uint8_t *out_buf, size_t *out_len, const uint8_t *payload,
			 size_t payload_len)
{
	struct lora_l2_header header = {0};
	int ret;

	if (!session_valid && !IS_ENABLED(CONFIG_LORA_NODE_ALLOW_PLAINTEXT)) {
		return -EACCES;
	}

	if (session_valid && seq_next >= seq_reserved_until) {
		ret = seq_reserve();
		if (ret < 0) {
			return ret;
		}
	}

	header.type_ver = lora_set_frame_type(LORA_FRAME_SENSOR_DATA) | LORA_PROTOCOL_VERSION;
	lora_hdr_src_set(&header, session_node_id);
	lora_hdr_seq_set(&header, seq_next);

	ret = lora_packet_encode(&header, payload, payload_len, session_valid ? session_key : NULL,
				 out_buf, LORA_MAX_PACKET_SF12, out_len);
	if (ret < 0) {
		return ret;
	}

	seq_next++;
	return 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int lora_node_init(void)
{
	const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("LoRa radio not ready");
		return -ENODEV;
	}

	struct lora_modem_config cfg = {
		.frequency = CONFIG_LORA_NODE_DEFAULT_FREQ,
		.bandwidth = bw_to_enum(CONFIG_LORA_NODE_DEFAULT_BW),
		.datarate = sf_to_enum(CONFIG_LORA_NODE_DEFAULT_SF),
		.coding_rate = CR_4_5,
		.preamble_len = 8,
		.tx_power = CONFIG_LORA_NODE_TX_POWER,
		.tx = true}; /* node is TX-only; sx12xx applies SetTxConfig only if tx */

	int ret = lora_config(lora_dev, &cfg);
	if (ret < 0) {
		LOG_ERR("lora_config failed: %d", ret);
		return ret;
	}

	if (psa_crypto_init() != PSA_SUCCESS) {
		LOG_ERR("psa_crypto_init failed");
		return -EIO;
	}

	/* Errors are logged and not fatal: without a loaded session the node stays
	 * unpaired (TX refused); without a counter record a random start is used.
	 */
	ret = settings_subsys_init();
	if (ret < 0) {
		LOG_ERR("settings_subsys_init failed: %d", ret);
	}
	ret = settings_register(&session_settings);
	if (ret < 0) {
		LOG_ERR("settings_register(session) failed: %d", ret);
	}
	ret = settings_register(&seq_settings);
	if (ret < 0) {
		LOG_ERR("settings_register(seq) failed: %d", ret);
	}
	ret = settings_load_subtree(CONFIG_LORA_NODE_SETTINGS_KEY);
	if (ret < 0) {
		LOG_ERR("loading session failed: %d", ret);
	}
	ret = settings_load_subtree(CONFIG_LORA_NODE_SEQ_SETTINGS_KEY);
	if (ret < 0) {
		LOG_ERR("loading frame counter failed: %d", ret);
	}

	if (!session_valid && sizeof(CONFIG_LORA_NODE_DEV_KEY) > 1) {
		if (sizeof(CONFIG_LORA_NODE_DEV_KEY) - 1 == 2 * sizeof(session_key) &&
		    hex2bin(CONFIG_LORA_NODE_DEV_KEY, 2 * sizeof(session_key), session_key,
			    sizeof(session_key)) == sizeof(session_key)) {
			session_node_id = CONFIG_LORA_NODE_DEV_NODE_ID;
			session_valid = true;
			LOG_WRN("using compiled-in DEV session key (insecure)");
		} else {
			LOG_ERR("LORA_NODE_DEV_KEY must be 32 hex chars");
		}
	}

	if (session_valid && !seq_loaded) {
		seq_start_random();
	}

	if (session_valid) {
		LOG_INF("LoRa node initialized (node_id=0x%04x, paired)", session_node_id);
	} else {
		LOG_INF("LoRa node initialized (unpaired)");
	}

	return 0;
}

int lora_node_transmit(const uint8_t *readings, size_t num_readings)
{
	const struct device *lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));

	size_t payload_len = num_readings * LORA_READING_SIZE;
	if (num_readings > LORA_PROTO_MAX_READINGS(LORA_MAX_PACKET_SF12)) {
		LOG_ERR("too many readings: %zu", num_readings);
		return -EFBIG;
	}

	uint8_t buf[LORA_MAX_PACKET_SF12];
	size_t buf_len;

	/* Held across encode and send so seqs reach the air in order */
	k_mutex_lock(&tx_lock, K_FOREVER);
	int ret = encode_packet(buf, &buf_len, readings, payload_len);

	if (ret < 0) {
		k_mutex_unlock(&tx_lock);
		return ret;
	}

	ret = lora_send(lora_dev, buf, buf_len);
	k_mutex_unlock(&tx_lock);
	if (ret < 0) {
		LOG_ERR("lora_send failed: %d", ret);
		return ret;
	}

	LOG_DBG("transmitted %zu bytes (%zu readings)", buf_len, num_readings);
	return 0;
}

uint16_t lora_node_get_id(void)
{
	return session_node_id;
}

int lora_node_save_session(uint16_t node_id, const uint8_t session_key_in[16])
{
	struct session_data data = {.node_id = node_id};
	memcpy(data.key, session_key_in, sizeof(data.key));

	int ret = settings_save_one(CONFIG_LORA_NODE_SETTINGS_KEY, &data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	session_node_id = node_id;
	memcpy(session_key, session_key_in, sizeof(session_key));
	session_valid = true;

	LOG_INF("session saved (node_id=0x%04x)", node_id);
	return 0;
}
