/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_prov
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include "lora_radio_internal.h"
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

#ifdef CONFIG_PSA_CRYPTO
#	include <psa/crypto.h>

/* Development Ed25519 key pair - DO NOT USE IN PRODUCTION */
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
#endif /* CONFIG_PSA_CRYPTO */

int lora_handle_prov_beacon(const struct lora_l2_header *hdr, const uint8_t *payload,
			    uint8_t payload_len)
{
#ifndef CONFIG_PSA_CRYPTO
	(void)hdr;
	(void)payload;
	(void)payload_len;
	LOG_WRN("provisioning requires PSA Crypto (Ed25519)");
	return -ENOTSUP;
#else
	(void)hdr;

	if (payload_len < 1) {
		return -EINVAL;
	}

	uint8_t caps_count = payload[0];
	if (caps_count > 4) {
		LOG_WRN("too many caps in beacon: %d", caps_count);
		return -EINVAL;
	}

	uint16_t node_id = lora_session_alloc_node_id();
	if (node_id == 0) {
		LOG_WRN("no free node IDs");
		return -ENOMEM;
	}

	uint8_t session_key[16];
	for (int i = 0; i < 16; i++) {
		session_key[i] = (uint8_t)sys_rand32_get();
	}

	struct lora_prov_response resp;
	memset(&resp, 0, sizeof(resp));
	resp.node_id = (uint8_t)node_id;
	memcpy(resp.session_key, session_key, 16);
	memcpy(resp.gateway_pubkey, gateway_ed25519_pk, 32);

	psa_key_attributes_t sign_attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t sign_key_id;
	psa_set_key_usage_flags(&sign_attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&sign_attr, PSA_ALG_ED25519);
	psa_set_key_type(&sign_attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS));

	psa_status_t s = psa_import_key(&sign_attr, gateway_ed25519_sk, 32, &sign_key_id);
	if (s != PSA_SUCCESS) {
		LOG_ERR("Ed25519 key import failed: %d", s);
		return -EIO;
	}

	struct lora_l2_header resp_hdr;
	memset(&resp_hdr, 0, sizeof(resp_hdr));
	resp_hdr.type_ver = (LORA_FRAME_PROV_RESPONSE << 4) | 0x01;
	resp_hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	resp_hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);

	uint8_t sign_buf[sizeof(struct lora_l2_header) + sizeof(struct lora_prov_response)];
	memcpy(sign_buf, &resp_hdr, sizeof(resp_hdr));
	memcpy(sign_buf + sizeof(resp_hdr), &resp, sizeof(resp));

	size_t sig_len;
	s = psa_sign_message(&sign_key_id, PSA_ALG_ED25519, sign_buf, sizeof(sign_buf),
			     resp.signature, sizeof(resp.signature), &sig_len);
	psa_destroy_key(sign_key_id);

	if (s != PSA_SUCCESS) {
		LOG_ERR("Ed25519 signing failed: %d", s);
		return -EIO;
	}

	struct lora_modem_config prov_cfg = {
		.frequency = 868000000,
		.bandwidth = BW_500_KHZ,
		.datarate = SF_7,
		.coding_rate = CR_4_5,
		.preamble_len = 8,
		.tx_power = 14,
		.tx = true,
	};
	lora_config(lora_radio_dev, &prov_cfg);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&resp_hdr, &resp, sizeof(resp), NULL, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}

	lora_send(lora_radio_dev, tx_buf, tx_len);

	struct lora_session *sess = lora_session_add(node_id, session_key);
	if (!sess) {
		return -ENOMEM;
	}

	LOG_INF("paired node 0x%04x (%d capabilities)", node_id, caps_count);
	return 0;
#endif /* CONFIG_PSA_CRYPTO */
}
