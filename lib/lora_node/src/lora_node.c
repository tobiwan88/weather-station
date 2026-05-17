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
#include <zephyr/settings/settings.h>
#include <zephyr/sys/crc.h>

#include <psa/crypto.h>

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
 * Packet encoding
 * -------------------------------------------------------------------------- */

#define LORA_FRAME_SENSOR_DATA 0x0
#define LORA_PROTOCOL_VERSION  0x1
#define LORA_FLAG_ENCRYPTED    BIT(1)

#define LORA_HEADER_SIZE  8
#define LORA_GCM_TAG_SIZE 12
#define LORA_CRC_SIZE     2
#define LORA_OVERHEAD     (LORA_HEADER_SIZE + LORA_GCM_TAG_SIZE + LORA_CRC_SIZE)

#define LORA_MAX_PACKET_SF12 51
#define LORA_MAX_PAYLOAD                                                                           \
	(LORA_MAX_PACKET_SF12 - LORA_HEADER_SIZE - LORA_GCM_TAG_SIZE - LORA_CRC_SIZE)

#define LORA_READING_SIZE 5

BUILD_ASSERT(LORA_MAX_PAYLOAD >= LORA_READING_SIZE, "max payload too small for one reading");

static int encode_packet(uint8_t *out_buf, uint8_t *out_len, const uint8_t *payload,
			 uint8_t payload_len)
{
	struct {
		uint8_t type_ver;
		uint8_t flags;
		uint8_t src_node[2];
		uint8_t dst_node[2];
		uint8_t seq_num[2];
	} __packed header;

	static uint16_t seq_counter;

	header.type_ver = (LORA_FRAME_SENSOR_DATA << 4) | LORA_PROTOCOL_VERSION;
	header.flags = 0;
	header.src_node[0] = (uint8_t)(session_node_id & 0xFF);
	header.src_node[1] = (uint8_t)((session_node_id >> 8) & 0xFF);
	header.dst_node[0] = 0;
	header.dst_node[1] = 0;
	header.seq_num[0] = (uint8_t)(seq_counter & 0xFF);
	header.seq_num[1] = (uint8_t)((seq_counter >> 8) & 0xFF);

	uint8_t buf[LORA_MAX_PACKET_SF12];
	uint8_t pos = 0;

	memcpy(buf + pos, &header, sizeof(header));
	pos += sizeof(header);

	if (session_valid && IS_ENABLED(CONFIG_PSA_CRYPTO)) {
		header.flags |= LORA_FLAG_ENCRYPTED;
		memcpy(buf, &header, sizeof(header));

		psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
		psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
		psa_set_key_bits(&attrs, 128);
		psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT);
		psa_set_key_algorithm(&attrs, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 12));

		psa_key_id_t key_id;
		psa_status_t status =
			psa_import_key(&attrs, session_key, sizeof(session_key), &key_id);
		if (status != PSA_SUCCESS) {
			LOG_ERR("PSA key import failed: %d", (int)status);
			return -EIO;
		}

		uint8_t nonce[12] = {0};
		nonce[0] = header.seq_num[0];
		nonce[1] = header.seq_num[1];

		size_t cipher_len;
		status = psa_aead_encrypt(key_id, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 12),
					  nonce, sizeof(nonce), buf, sizeof(header), payload,
					  payload_len, buf + pos, sizeof(buf) - pos, &cipher_len);
		psa_destroy_key(key_id);

		if (status != PSA_SUCCESS) {
			LOG_ERR("PSA encrypt failed: %d", (int)status);
			return -EIO;
		}
		pos += cipher_len;
	} else {
		memcpy(buf + pos, payload, payload_len);
		pos += payload_len;
	}

	uint16_t crc = crc16_ccitt(0xFFFF, buf, pos);
	buf[pos++] = (uint8_t)(crc & 0xFF);
	buf[pos++] = (uint8_t)((crc >> 8) & 0xFF);

	memcpy(out_buf, buf, pos);
	*out_len = pos;

	seq_counter++;
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

	struct lora_modem_config cfg = {.frequency = CONFIG_LORA_NODE_DEFAULT_FREQ,
					.bandwidth = bw_to_enum(CONFIG_LORA_NODE_DEFAULT_BW),
					.datarate = sf_to_enum(CONFIG_LORA_NODE_DEFAULT_SF),
					.coding_rate = CR_4_5,
					.preamble_len = 8,
					.tx_power = CONFIG_LORA_NODE_TX_POWER,
					.tx = false};

	int ret = lora_config(lora_dev, &cfg);
	if (ret < 0) {
		LOG_ERR("lora_config failed: %d", ret);
		return ret;
	}

	psa_crypto_init();

	settings_subsys_init();
	settings_register(&session_settings);
	settings_load_subtree(CONFIG_LORA_NODE_SETTINGS_KEY);

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
	if (payload_len > LORA_MAX_PAYLOAD) {
		LOG_ERR("payload too large: %zu > %d", payload_len, LORA_MAX_PAYLOAD);
		return -EFBIG;
	}

	uint8_t buf[LORA_MAX_PACKET_SF12];
	uint8_t buf_len;

	int ret = encode_packet(buf, &buf_len, readings, (uint8_t)payload_len);
	if (ret < 0) {
		return ret;
	}

	ret = lora_send(lora_dev, buf, buf_len);
	if (ret < 0) {
		LOG_ERR("lora_send failed: %d", ret);
		return ret;
	}

	LOG_DBG("transmitted %d bytes (%zu readings)", buf_len, num_readings);
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
