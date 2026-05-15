/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_packet
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_PSA_CRYPTO
#	include <psa/crypto.h>
#endif

#include <lora_radio/lora_frame.h>

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

int lora_packet_encode(struct lora_l2_header *hdr, const void *payload, uint8_t payload_len,
		       const uint8_t session_key[16], uint8_t *out_buf, uint8_t *out_len)
{
	uint8_t hdr_len = sizeof(struct lora_l2_header);

	memcpy(out_buf, hdr, hdr_len);
	memcpy(out_buf + hdr_len, payload, payload_len);

#ifdef CONFIG_PSA_CRYPTO
	if (session_key != NULL) {
		uint8_t total = hdr_len + payload_len + 12;

		if (total > LORA_MAX_PACKET_SF7) {
			LOG_ERR("packet too large: %d > %d", total, LORA_MAX_PACKET_SF7);
			return -ENOSPC;
		}

		/* Set ENCRYPTED flag BEFORE encryption so AAD matches on both sides */
		hdr->flags |= LORA_FLAG_ENCRYPTED;
		memcpy(out_buf, hdr, hdr_len);

		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
		psa_key_id_t key_id;
		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_GCM);
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);

		psa_status_t s = psa_import_key(&attr, session_key, 16, &key_id);
		if (s != PSA_SUCCESS) {
			LOG_ERR("psa_import_key failed: %d", s);
			return -EIO;
		}

		uint8_t nonce[12] = {0};
		nonce[0] = hdr->seq_num[0];
		nonce[1] = hdr->seq_num[1];

		size_t tag_len;
		s = psa_aead_encrypt(&key_id, PSA_ALG_GCM, nonce, sizeof(nonce), out_buf, hdr_len,
				     out_buf + hdr_len, payload_len, out_buf + hdr_len,
				     LORA_MAX_PAYLOAD_SF7, &tag_len);
		psa_destroy_key(key_id);

		if (s != PSA_SUCCESS) {
			LOG_ERR("psa_aead_encrypt failed: %d", s);
			return -EIO;
		}

		hdr->flags |= LORA_FLAG_ENCRYPTED;
		memcpy(out_buf, hdr, hdr_len);
		*out_len = hdr_len + payload_len + tag_len;
	} else {
		*out_len = hdr_len + payload_len;
	}
#else
	*out_len = hdr_len + payload_len;
#endif

	uint16_t crc = crc16_ccitt(out_buf, *out_len);
	out_buf[(*out_len)++] = crc & 0xFF;
	out_buf[(*out_len)++] = (crc >> 8) & 0xFF;
	return 0;
}

int lora_packet_decode(const uint8_t *in_buf, uint8_t in_len, const uint8_t session_key[16],
		       struct lora_l2_header *hdr, uint8_t *payload, uint8_t *payload_len)
{
	if (in_len < sizeof(struct lora_l2_header) + 2) {
		return -EINVAL;
	}

	uint16_t crc_rx = (uint16_t)in_buf[in_len - 2] | ((uint16_t)in_buf[in_len - 1] << 8);
	uint16_t crc_calc = crc16_ccitt(in_buf, in_len - 2);
	if (crc_rx != crc_calc) {
		LOG_WRN("CRC mismatch");
		return -EBADMSG;
	}

	uint8_t hdr_len = sizeof(struct lora_l2_header);
	memcpy(hdr, in_buf, hdr_len);
	uint8_t enc_len = in_len - hdr_len - 2;
	bool encrypted = (hdr->flags & LORA_FLAG_ENCRYPTED) != 0;

#ifdef CONFIG_PSA_CRYPTO
	if (encrypted) {
		if (session_key == NULL) {
			LOG_WRN("encrypted frame but no key for node 0x%04x", hdr->src_node);
			return -EPERM;
		}
		if (enc_len < 12) {
			return -EINVAL;
		}

		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
		psa_key_id_t key_id;
		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DECRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_GCM);
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);

		psa_status_t s = psa_import_key(&attr, session_key, 16, &key_id);
		if (s != PSA_SUCCESS) {
			return -EIO;
		}

		uint8_t nonce[12] = {0};
		nonce[0] = hdr->seq_num[0];
		nonce[1] = hdr->seq_num[1];

		size_t dec_len;
		s = psa_aead_decrypt(&key_id, PSA_ALG_GCM, nonce, sizeof(nonce), in_buf, hdr_len,
				     in_buf + hdr_len, enc_len, payload, LORA_MAX_PAYLOAD_SF7,
				     &dec_len);
		psa_destroy_key(key_id);

		if (s != PSA_SUCCESS) {
			LOG_WRN("GCM decrypt failed: %d (node 0x%04x)", s, hdr->src_node);
			return -EBADMSG;
		}
		*payload_len = dec_len;
	} else {
		if (enc_len > LORA_MAX_PAYLOAD_SF7) {
			return -EINVAL;
		}
		memcpy(payload, in_buf + hdr_len, enc_len);
		*payload_len = enc_len;
	}
#else
	if (encrypted) {
		LOG_WRN("encrypted frame dropped (PSA Crypto unavailable)");
		return -ENOTSUP;
	}
	if (enc_len > LORA_MAX_PAYLOAD_SF7) {
		return -EINVAL;
	}
	memcpy(payload, in_buf + hdr_len, enc_len);
	*payload_len = enc_len;
#endif

	return 0;
}
