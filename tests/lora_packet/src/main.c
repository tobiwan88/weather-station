/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/lora_packet)
 * @brief Unit tests for LoRa packet encode/decode and header helpers.
 */

#include <lora_proto/lora_packet.h>
#include <psa/crypto.h>
#include <string.h>
#include <zephyr/ztest.h>

#include <zephyr/sys/crc.h>

/**
 * @brief Frame type extraction from type_ver byte.
 */
ZTEST(lora_packet_suite, test_frame_type_extract)
{
	zassert_equal(lora_frame_type(0x00), 0x0, "SENSOR_DATA type");
	zassert_equal(lora_frame_type(0x10), 0x1, "SENSOR_DATA_ACK type");
	zassert_equal(lora_frame_type(0x20), 0x2, "RPC_CMD type");
	zassert_equal(lora_frame_type(0x40), 0x4, "FOTA_CHUNK type");
	zassert_equal(lora_frame_type(0x50), 0x5, "FOTA_CHUNK_ACK type");
	zassert_equal(lora_frame_type(0x80), 0x8, "ACK type");
}

/**
 * @brief Frame version extraction from type_ver byte.
 */
ZTEST(lora_packet_suite, test_frame_version_extract)
{
	zassert_equal(lora_frame_version(0x01), 0x1, "version 1");
	zassert_equal(lora_frame_version(0x0F), 0xF, "version 15");
	zassert_equal(lora_frame_version(0x40), 0x0, "version 0");
}

/**
 * @brief Frame type and version packing.
 */
ZTEST(lora_packet_suite, test_frame_type_version_pack)
{
	uint8_t type_ver = lora_set_frame_type(LORA_FRAME_RPC_CMD) | lora_set_version(1);

	zassert_equal(lora_frame_type(type_ver), LORA_FRAME_RPC_CMD, "packed type");
	zassert_equal(lora_frame_version(type_ver), 1, "packed version");
}

/**
 * @brief Header struct size is exactly 8 bytes.
 */
static const uint8_t key_a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint8_t key_b[16] = {0xA5};

static void *suite_setup(void)
{
	zassert_equal(psa_crypto_init(), PSA_SUCCESS, "psa init");
	return NULL;
}

ZTEST_SUITE(lora_packet_suite, NULL, suite_setup, NULL, NULL, NULL);

static void make_hdr(struct lora_l2_header *h, uint16_t src, uint16_t dst, uint32_t seq,
		     uint8_t flags)
{
	memset(h, 0, sizeof(*h));
	h->type_ver = lora_set_frame_type(LORA_FRAME_SENSOR_DATA) | lora_set_version(1);
	h->flags = flags;
	lora_hdr_src_set(h, src);
	lora_hdr_dst_set(h, dst);
	lora_hdr_seq_set(h, seq);
}

ZTEST(lora_packet_suite, test_header_size)
{
	zassert_equal(sizeof(struct lora_l2_header), 10, "header must be 10 bytes");
}

ZTEST(lora_packet_suite, test_seq_helpers)
{
	struct lora_l2_header h = {0};

	lora_hdr_seq_set(&h, 0x01020304);
	zassert_equal(h.seq_num[0], 0x04, "LE byte 0");
	zassert_equal(h.seq_num[3], 0x01, "LE byte 3");
	zassert_equal(lora_hdr_seq_get(&h), 0x01020304, "round trip");
}

ZTEST(lora_packet_suite, test_plaintext_round_trip)
{
	struct lora_l2_header h, d;
	const uint8_t pl[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 0x0007, 0, 0x12345678, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), NULL, buf, sizeof(buf), &n));
	zassert_equal(n, 10 + sizeof(pl) + 2, "plain length");
	zassert_ok(lora_packet_decode(buf, n, NULL, &d, out, sizeof(out), &m));
	zassert_equal(m, sizeof(pl));
	zassert_mem_equal(out, pl, sizeof(pl));
	zassert_equal(lora_hdr_seq_get(&d), 0x12345678);
	zassert_equal(d.flags & LORA_FLAG_ENCRYPTED, 0);
}

ZTEST(lora_packet_suite, test_encrypted_round_trip)
{
	struct lora_l2_header h, d;
	const uint8_t pl[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 0x0007, 0, 42, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, buf, sizeof(buf), &n));
	zassert_equal(n, 10 + sizeof(pl) + 12 + 2, "encrypted length");
	zassert_not_equal(memcmp(buf + 10, pl, sizeof(pl)), 0, "payload must be encrypted");
	zassert_ok(lora_packet_decode(buf, n, key_a, &d, out, sizeof(out), &m));
	zassert_equal(m, sizeof(pl));
	zassert_mem_equal(out, pl, sizeof(pl));
	zassert_true(d.flags & LORA_FLAG_ENCRYPTED);
}

/* Re-seal the CRC after tampering so only the AEAD check can reject the frame. */
static void fix_crc(uint8_t *buf, size_t n)
{
	uint16_t crc = crc16_ccitt(0xFFFF, buf, n - 2);

	buf[n - 2] = crc & 0xFF;
	buf[n - 1] = crc >> 8;
}

ZTEST(lora_packet_suite, test_tampered_tag)
{
	struct lora_l2_header h, d;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, buf, sizeof(buf), &n));
	buf[n - 3] ^= 0x01; /* last tag byte */
	fix_crc(buf, n);
	zassert_equal(lora_packet_decode(buf, n, key_a, &d, out, sizeof(out), &m), -EBADMSG);
}

/* A key is supplied but the frame is plaintext: must not be accepted as authentic. */
ZTEST(lora_packet_suite, test_plaintext_with_key_rejected)
{
	struct lora_l2_header h, d;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), NULL, buf, sizeof(buf), &n));
	zassert_equal(lora_packet_decode(buf, n, key_a, &d, out, sizeof(out), &m), -EPERM);
	/* The same frame is fine when the caller explicitly accepts plaintext */
	zassert_ok(lora_packet_decode(buf, n, NULL, &d, out, sizeof(out), &m));
	zassert_equal(m, sizeof(pl));
}

ZTEST(lora_packet_suite, test_tampered_header)
{
	struct lora_l2_header h, d;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, buf, sizeof(buf), &n));
	buf[4] ^= 0x01; /* dst node: not part of the nonce for an uplink frame */
	fix_crc(buf, n);
	zassert_equal(lora_packet_decode(buf, n, key_a, &d, out, sizeof(out), &m), -EBADMSG);
}

ZTEST(lora_packet_suite, test_wrong_key)
{
	struct lora_l2_header h, d;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, buf, sizeof(buf), &n));
	zassert_equal(lora_packet_decode(buf, n, key_b, &d, out, sizeof(out), &m), -EBADMSG);
	zassert_equal(lora_packet_decode(buf, n, NULL, &d, out, sizeof(out), &m), -EPERM);
}

ZTEST(lora_packet_suite, test_size_limits)
{
	struct lora_l2_header h;
	uint8_t pl[6 * LORA_READING_SIZE] = {0};
	uint8_t buf[LORA_MAX_PACKET_SF12];
	size_t n;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_equal(LORA_PROTO_MAX_READINGS(LORA_MAX_PACKET_SF12), 5);
	zassert_ok(lora_packet_encode(&h, pl, 5 * LORA_READING_SIZE, key_a, buf, sizeof(buf), &n));
	/* 10 header + 25 payload + 12 tag + 2 CRC */
	zassert_equal(n, 49, "5 readings");
	zassert_equal(
		lora_packet_encode(&h, pl, 6 * LORA_READING_SIZE, key_a, buf, sizeof(buf), &n),
		-EMSGSIZE);
	zassert_equal(lora_packet_encode(NULL, pl, 5, key_a, buf, sizeof(buf), &n), -EINVAL);
}

ZTEST(lora_packet_suite, test_seq_changes_ciphertext)
{
	struct lora_l2_header h;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t b1[64], b2[64];
	size_t n1, n2;

	make_hdr(&h, 7, 0, 1, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, b1, sizeof(b1), &n1));
	make_hdr(&h, 7, 0, 2, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, b2, sizeof(b2), &n2));
	zassert_equal(n1, n2);
	zassert_not_equal(memcmp(b1 + 10, b2 + 10, sizeof(pl)), 0, "ciphertext differs by seq");
}

ZTEST(lora_packet_suite, test_direction_changes_ciphertext)
{
	struct lora_l2_header h;
	const uint8_t pl[5] = {1, 2, 3, 4, 5};
	uint8_t b1[64], b2[64];
	size_t n1, n2;

	/* node 7 is src for the uplink and dst for the downlink */
	make_hdr(&h, 7, 0, 9, 0);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, b1, sizeof(b1), &n1));
	make_hdr(&h, 0, 7, 9, LORA_FLAG_DIR_DOWN);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, b2, sizeof(b2), &n2));
	zassert_not_equal(memcmp(b1 + 10, b2 + 10, sizeof(pl)), 0, "ciphertext differs by dir");
}

ZTEST(lora_packet_suite, test_downlink_round_trip)
{
	struct lora_l2_header h, d;
	const uint8_t pl[5] = {9, 8, 7, 6, 5};
	uint8_t buf[64], out[64];
	size_t n, m;

	make_hdr(&h, 0, 7, 3, LORA_FLAG_DIR_DOWN);
	zassert_ok(lora_packet_encode(&h, pl, sizeof(pl), key_a, buf, sizeof(buf), &n));
	zassert_ok(lora_packet_decode(buf, n, key_a, &d, out, sizeof(out), &m));
	zassert_mem_equal(out, pl, sizeof(pl));
}

ZTEST(lora_packet_suite, test_decode_crc_mismatch)
{
	struct lora_l2_header hdr;
	uint8_t buf[20] = {0};
	uint8_t payload[32];
	size_t len;

	buf[0] = 0x01;
	zassert_equal(
		lora_packet_decode(buf, sizeof(buf), NULL, &hdr, payload, sizeof(payload), &len),
		-EBADMSG);
}

ZTEST(lora_packet_suite, test_decode_too_short)
{
	struct lora_l2_header hdr;
	uint8_t buf[5] = {0};
	uint8_t payload[32];
	size_t len;

	zassert_equal(
		lora_packet_decode(buf, sizeof(buf), NULL, &hdr, payload, sizeof(payload), &len),
		-EINVAL);
}

/**
 * @brief Flag constants are correct bit positions.
 */
ZTEST(lora_packet_suite, test_flag_constants)
{
	zassert_equal(LORA_FLAG_ACK_REQ, 0x01, "ACK_REQ bit");
	zassert_equal(LORA_FLAG_ENCRYPTED, 0x02, "ENCRYPTED bit");
	zassert_equal(LORA_FLAG_FRAGMENTED, 0x04, "FRAGMENTED bit");
	zassert_equal(LORA_FLAG_DIR_DOWN, 0x08, "DIR_DOWN bit");
}

/**
 * @brief RPC command ID constants are correct.
 */
ZTEST(lora_packet_suite, test_rpc_command_ids)
{
	zassert_equal(LORA_RPC_PING, 0x00, "PING");
	zassert_equal(LORA_RPC_GET_VERSION, 0x01, "GET_VERSION");
	zassert_equal(LORA_RPC_REBOOT, 0xFF, "REBOOT");
	zassert_equal(LORA_RPC_FOTA_START, 0x30, "FOTA_START");
	zassert_equal(LORA_RPC_FOTA_CANCEL, 0x31, "FOTA_CANCEL");
}

/**
 * @brief Payload struct sizes are correct.
 */
ZTEST(lora_packet_suite, test_payload_struct_sizes)
{
	zassert_equal(sizeof(struct lora_sensor_data_ack), 2, "data_ack size");
	zassert_equal(sizeof(struct lora_rpc_cmd), 9, "rpc_cmd size");
	zassert_equal(sizeof(struct lora_rpc_resp), 10, "rpc_resp size");
	zassert_equal(sizeof(struct lora_fota_chunk_ack), 9, "fota_chunk_ack size");
	zassert_equal(sizeof(struct lora_ack), 5, "ack size");
}

/**
 * @brief Packet size constants are consistent.
 */
ZTEST(lora_packet_suite, test_packet_size_constants)
{
	zassert_equal(LORA_OVERHEAD, 24, "overhead = header(10) + GCM tag(12) + CRC(2)");
	zassert_equal(LORA_MAX_PAYLOAD_SF12, LORA_MAX_PACKET_SF12 - LORA_OVERHEAD, "SF12 payload");
	zassert_equal(LORA_MAX_PAYLOAD_SF7, LORA_MAX_PACKET_SF7 - LORA_OVERHEAD, "SF7 payload");
	zassert_equal(LORA_READING_SIZE, 5, "reading = type(1) + Q31(4)");
}
