/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_proto
#define LOG_LEVEL       CONFIG_LORA_PROTO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include <psa/crypto.h>

#include <lora_proto/lora_packet.h>

#define LORA_KEY_SIZE 16
#define LORA_AEAD_ALG PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, LORA_GCM_TAG_SIZE)

BUILD_ASSERT(LORA_PROTO_OVERHEAD == 24, "header 10 + tag 12 + CRC 2");

/* Zephyr's mbedTLS / TF-PSA-Crypto build has no threading option (no
 * MBEDTLS_THREADING_C Kconfig), so concurrent psa_* calls are not safe. The
 * codec is called from the RX thread, the pending workqueue, shell and FOTA:
 * serialize every PSA access made here.
 */
static K_MUTEX_DEFINE(psa_lock);

/* Nonce: node_id(2 LE) | dir | 0 | seq(4 LE) | 0,0,0,0. The node id is the
 * non-gateway end: src for uplink, dst for downlink.
 */
static void build_nonce(const struct lora_l2_header *hdr, uint8_t nonce[12])
{
	bool down = (hdr->flags & LORA_FLAG_DIR_DOWN) != 0;

	memset(nonce, 0, 12);
	sys_put_le16(down ? lora_hdr_dst_get(hdr) : lora_hdr_src_get(hdr), &nonce[0]);
	nonce[2] = down ? 1 : 0;
	sys_put_le32(lora_hdr_seq_get(hdr), &nonce[4]);
}

static int import_key(const uint8_t *key, psa_key_usage_t usage, psa_key_id_t *id)
{
	psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&attrs, 128);
	psa_set_key_usage_flags(&attrs, usage);
	psa_set_key_algorithm(&attrs, LORA_AEAD_ALG);

	psa_status_t st = psa_import_key(&attrs, key, LORA_KEY_SIZE, id);

	if (st != PSA_SUCCESS) {
		LOG_ERR("psa_import_key failed: %d", (int)st);
		return -EIO;
	}
	return 0;
}

int lora_packet_encode(const struct lora_l2_header *hdr, const void *payload, size_t len,
		       const uint8_t *key, uint8_t *out, size_t out_size, size_t *out_len)
{
	if (hdr == NULL || out == NULL || out_len == NULL || (payload == NULL && len > 0)) {
		return -EINVAL;
	}

	size_t body = len + (key != NULL ? LORA_GCM_TAG_SIZE : 0);
	size_t total = LORA_HEADER_SIZE + body + LORA_CRC_SIZE;

	if (total > out_size) {
		return -EMSGSIZE;
	}

	memcpy(out, hdr, LORA_HEADER_SIZE);

	if (key != NULL) {
		struct lora_l2_header *oh = (struct lora_l2_header *)out;
		uint8_t nonce[12];
		psa_key_id_t kid;
		size_t clen;

		oh->flags |= LORA_FLAG_ENCRYPTED;
		build_nonce(oh, nonce);

		k_mutex_lock(&psa_lock, K_FOREVER);
		if (psa_crypto_init() != PSA_SUCCESS) {
			k_mutex_unlock(&psa_lock);
			return -EIO;
		}
		int rc = import_key(key, PSA_KEY_USAGE_ENCRYPT, &kid);

		if (rc < 0) {
			k_mutex_unlock(&psa_lock);
			return rc;
		}

		psa_status_t st = psa_aead_encrypt(kid, LORA_AEAD_ALG, nonce, sizeof(nonce), out,
						   LORA_HEADER_SIZE, payload, len,
						   out + LORA_HEADER_SIZE, body, &clen);

		psa_destroy_key(kid);
		k_mutex_unlock(&psa_lock);
		if (st != PSA_SUCCESS || clen != body) {
			LOG_ERR("psa_aead_encrypt failed: %d", (int)st);
			return -EIO;
		}
	} else if (len > 0) {
		memcpy(out + LORA_HEADER_SIZE, payload, len);
	}

	size_t pos = LORA_HEADER_SIZE + body;
	uint16_t crc = crc16_ccitt(0xFFFF, out, pos);

	sys_put_le16(crc, &out[pos]);
	*out_len = pos + LORA_CRC_SIZE;
	return 0;
}

int lora_packet_decode(const uint8_t *in, size_t in_len, const uint8_t *key,
		       struct lora_l2_header *hdr, uint8_t *payload, size_t payload_size,
		       size_t *payload_len)
{
	if (in == NULL || hdr == NULL || payload == NULL || payload_len == NULL) {
		return -EINVAL;
	}
	if (in_len < LORA_HEADER_SIZE + LORA_CRC_SIZE) {
		return -EINVAL;
	}

	size_t pos = in_len - LORA_CRC_SIZE;

	if (sys_get_le16(&in[pos]) != crc16_ccitt(0xFFFF, in, pos)) {
		LOG_WRN("CRC mismatch");
		return -EBADMSG;
	}

	memcpy(hdr, in, LORA_HEADER_SIZE);

	size_t body = pos - LORA_HEADER_SIZE;

	if ((hdr->flags & LORA_FLAG_ENCRYPTED) == 0) {
		if (key != NULL) {
			/* A key was supplied, so the caller expects an authenticated frame */
			LOG_WRN("plaintext frame where an encrypted one is required (node 0x%04x)",
				lora_hdr_src_get(hdr));
			return -EPERM;
		}
		if (body > payload_size) {
			return -EMSGSIZE;
		}
		memcpy(payload, in + LORA_HEADER_SIZE, body);
		*payload_len = body;
		return 0;
	}

	if (key == NULL) {
		LOG_WRN("encrypted frame but no key for node 0x%04x", lora_hdr_src_get(hdr));
		return -EPERM;
	}
	if (body < LORA_GCM_TAG_SIZE) {
		return -EINVAL;
	}
	if (body - LORA_GCM_TAG_SIZE > payload_size) {
		return -EMSGSIZE;
	}

	uint8_t nonce[12];
	psa_key_id_t kid;
	size_t plen;

	build_nonce(hdr, nonce);

	k_mutex_lock(&psa_lock, K_FOREVER);
	if (psa_crypto_init() != PSA_SUCCESS) {
		k_mutex_unlock(&psa_lock);
		return -EIO;
	}
	int rc = import_key(key, PSA_KEY_USAGE_DECRYPT, &kid);

	if (rc < 0) {
		k_mutex_unlock(&psa_lock);
		return rc;
	}

	psa_status_t st =
		psa_aead_decrypt(kid, LORA_AEAD_ALG, nonce, sizeof(nonce), in, LORA_HEADER_SIZE,
				 in + LORA_HEADER_SIZE, body, payload, payload_size, &plen);

	psa_destroy_key(kid);
	k_mutex_unlock(&psa_lock);
	if (st == PSA_ERROR_INVALID_SIGNATURE) {
		LOG_WRN("GCM auth failed (node 0x%04x)", lora_hdr_src_get(hdr));
		return -EBADMSG;
	}
	if (st != PSA_SUCCESS) {
		LOG_ERR("psa_aead_decrypt failed: %d", (int)st);
		return -EIO;
	}
	*payload_len = plen;
	return 0;
}
