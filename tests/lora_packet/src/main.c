/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/lora_packet)
 * @brief Unit tests for LoRa packet encode/decode and header helpers.
 */

#include <lora_radio/lora_frame.h>
#include <string.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(lora_packet_suite, NULL, NULL, NULL, NULL, NULL);

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
ZTEST(lora_packet_suite, test_header_size)
{
	zassert_equal(sizeof(struct lora_l2_header), 8, "header must be 8 bytes");
}

/**
 * @brief Encode and decode unencrypted packet round-trip.
 */
ZTEST(lora_packet_suite, test_encode_decode_unencrypted)
{
	struct lora_l2_header hdr = {0};

	hdr.type_ver = lora_set_frame_type(LORA_FRAME_SENSOR_DATA) | lora_set_version(1);
	hdr.flags = 0;
	hdr.src_node[0] = 0x12;
	hdr.src_node[1] = 0x34;
	hdr.dst_node[0] = 0x56;
	hdr.dst_node[1] = 0x78;
	hdr.seq_num[0] = 0xAB;
	hdr.seq_num[1] = 0xCD;

	const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	uint8_t out_buf[LORA_MAX_PACKET_SF7];
	uint8_t out_len;

	int ret = lora_packet_encode(&hdr, payload, sizeof(payload), NULL, out_buf, &out_len);
	zassert_equal(ret, 0, "encode failed: %d", ret);
	/* 8 header + 5 payload + 2 CRC = 15 */
	zassert_equal(out_len, 15, "encoded length %d, expected 15", out_len);

	struct lora_l2_header decoded_hdr = {0};
	uint8_t decoded_payload[sizeof(payload)];
	uint8_t decoded_len;

	ret = lora_packet_decode(out_buf, out_len, NULL, &decoded_hdr, decoded_payload,
				 &decoded_len);
	zassert_equal(ret, 0, "decode failed: %d", ret);
	zassert_equal(decoded_len, sizeof(payload), "decoded length %d", decoded_len);
	zassert_mem_equal(decoded_payload, payload, sizeof(payload), "payload mismatch");
	zassert_equal(decoded_hdr.type_ver, hdr.type_ver, "type_ver mismatch");
	zassert_equal(decoded_hdr.flags, hdr.flags, "flags mismatch");
}

/**
 * @brief Decode with CRC mismatch returns -EBADMSG.
 */
ZTEST(lora_packet_suite, test_decode_crc_mismatch)
{
	uint8_t buf[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* header */
			 0x01, 0x02,                                     /* payload */
			 0xFF, 0xFF};                                    /* bad CRC */
	struct lora_l2_header hdr = {0};
	uint8_t payload[16];
	uint8_t payload_len;

	int ret = lora_packet_decode(buf, sizeof(buf), NULL, &hdr, payload, &payload_len);
	zassert_equal(ret, -EBADMSG, "decode with bad CRC returned %d, expected -EBADMSG", ret);
}

/**
 * @brief Decode with undersized buffer returns -EINVAL.
 */
ZTEST(lora_packet_suite, test_decode_too_short)
{
	uint8_t buf[] = {0x00, 0x00, 0x00}; /* less than header + CRC */
	struct lora_l2_header hdr = {0};
	uint8_t payload[16];
	uint8_t payload_len;

	int ret = lora_packet_decode(buf, sizeof(buf), NULL, &hdr, payload, &payload_len);
	zassert_equal(ret, -EINVAL, "decode too short returned %d, expected -EINVAL", ret);
}

/**
 * @brief Flag constants are correct bit positions.
 */
ZTEST(lora_packet_suite, test_flag_constants)
{
	zassert_equal(LORA_FLAG_ACK_REQ, 0x01, "ACK_REQ bit");
	zassert_equal(LORA_FLAG_ENCRYPTED, 0x02, "ENCRYPTED bit");
	zassert_equal(LORA_FLAG_FRAGMENTED, 0x04, "FRAGMENTED bit");
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
	zassert_equal(sizeof(struct lora_ack), 3, "ack size");
}

/**
 * @brief Packet size constants are consistent.
 */
ZTEST(lora_packet_suite, test_packet_size_constants)
{
	zassert_equal(LORA_OVERHEAD, 20, "overhead = header(8) + GCM tag(12)");
	zassert_equal(LORA_MAX_PAYLOAD_SF12, LORA_MAX_PACKET_SF12 - LORA_OVERHEAD, "SF12 payload");
	zassert_equal(LORA_MAX_PAYLOAD_SF7, LORA_MAX_PACKET_SF7 - LORA_OVERHEAD, "SF7 payload");
	zassert_equal(LORA_READING_SIZE, 5, "reading = type(1) + Q31(4)");
}
