/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_PROTO_LORA_FRAME_H_
#define LORA_PROTO_LORA_FRAME_H_

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Frame type IDs (4-bit field in type_ver byte)
 * -------------------------------------------------------------------------- */

#define LORA_FRAME_SENSOR_DATA     0x0
#define LORA_FRAME_SENSOR_DATA_ACK 0x1
#define LORA_FRAME_RPC_CMD         0x2
#define LORA_FRAME_RPC_RESP        0x3
#define LORA_FRAME_FOTA_CHUNK      0x4
#define LORA_FRAME_FOTA_CHUNK_ACK  0x5
/* 0x6, 0x7: reserved (formerly OTA pairing) */
#define LORA_FRAME_ACK 0x8

/* --------------------------------------------------------------------------
 * Flag bit defines (flags byte)
 * -------------------------------------------------------------------------- */

#define LORA_FLAG_ACK_REQ    BIT(0)
#define LORA_FLAG_ENCRYPTED  BIT(1)
#define LORA_FLAG_FRAGMENTED BIT(2)
#define LORA_FLAG_DIR_DOWN   BIT(3) /* gateway -> node frame (part of the GCM nonce) */

/* --------------------------------------------------------------------------
 * L2 header (10 bytes, packed)
 * --------------------------------------------------------------------------
 * Byte 0:    type (4b) + protocol_version (4b)
 * Byte 1:    flags
 * Bytes 2-3: src_node (uint16 LE)
 * Bytes 4-5: dst_node (uint16 LE)
 * Bytes 6-9: seq_num (uint32 LE)
 */

struct lora_l2_header {
	uint8_t type_ver;
	uint8_t flags;
	uint8_t src_node[2];
	uint8_t dst_node[2];
	uint8_t seq_num[4];
} __packed;

BUILD_ASSERT(sizeof(struct lora_l2_header) == 10, "lora_l2_header must be exactly 10 bytes");

static inline uint32_t lora_hdr_seq_get(const struct lora_l2_header *hdr)
{
	return sys_get_le32(hdr->seq_num);
}

static inline void lora_hdr_seq_set(struct lora_l2_header *hdr, uint32_t seq)
{
	sys_put_le32(seq, hdr->seq_num);
}

static inline uint16_t lora_hdr_src_get(const struct lora_l2_header *hdr)
{
	return sys_get_le16(hdr->src_node);
}

static inline void lora_hdr_src_set(struct lora_l2_header *hdr, uint16_t node)
{
	sys_put_le16(node, hdr->src_node);
}

static inline uint16_t lora_hdr_dst_get(const struct lora_l2_header *hdr)
{
	return sys_get_le16(hdr->dst_node);
}

static inline void lora_hdr_dst_set(struct lora_l2_header *hdr, uint16_t node)
{
	sys_put_le16(node, hdr->dst_node);
}

/* --------------------------------------------------------------------------
 * Inline header helper functions
 * -------------------------------------------------------------------------- */

static inline uint8_t lora_frame_type(uint8_t type_ver)
{
	return (type_ver >> 4) & 0x0F;
}

static inline uint8_t lora_frame_version(uint8_t type_ver)
{
	return type_ver & 0x0F;
}

static inline uint8_t lora_set_frame_type(uint8_t type)
{
	return (type & 0x0F) << 4;
}

static inline uint8_t lora_set_version(uint8_t ver)
{
	return ver & 0x0F;
}

/* --------------------------------------------------------------------------
 * Payload structs
 * -------------------------------------------------------------------------- */

/* Sensor data acknowledgment (1 reading per slot) */
struct lora_sensor_data_ack {
	uint8_t last_seq[2];
} __packed;

/* RPC command */
struct lora_rpc_cmd {
	uint8_t cmd_id;
	uint8_t params[8];
} __packed;

/* RPC response */
struct lora_rpc_resp {
	uint8_t cmd_id;
	uint8_t status;
	uint8_t data[8];
} __packed;

/* FOTA chunk */
struct lora_fota_chunk {
	uint32_t offset;
	uint8_t data[];
} __packed;

/* FOTA chunk acknowledgment */
struct lora_fota_chunk_ack {
	uint8_t offset[4];
	uint8_t expected_offset[4];
	uint8_t status;
} __packed;

/* Standalone ACK/NACK */
struct lora_ack {
	uint8_t seq_num[4];
	uint8_t status;
} __packed;

/* --------------------------------------------------------------------------
 * Packet size constants
 * -------------------------------------------------------------------------- */

/* Maximum LoRa packet payload at SF12 (lowest data rate) */
#define LORA_MAX_PACKET_SF12 51

/* Maximum LoRa packet payload at SF7 (highest data rate) */
#define LORA_MAX_PACKET_SF7 255

/* Protocol overhead: 10-byte L2 header + 12-byte GCM tag + 2-byte CRC16 */
#define LORA_HEADER_SIZE  10
#define LORA_GCM_TAG_SIZE 12
#define LORA_CRC_SIZE     2
#define LORA_OVERHEAD     (LORA_HEADER_SIZE + LORA_GCM_TAG_SIZE + LORA_CRC_SIZE)

/* Maximum payload available for frame-type-specific data */
#define LORA_MAX_PAYLOAD_SF12 (LORA_MAX_PACKET_SF12 - LORA_OVERHEAD)
#define LORA_MAX_PAYLOAD_SF7  (LORA_MAX_PACKET_SF7 - LORA_OVERHEAD)

/* Size of one compact sensor reading on the wire: type (1) + Q31 (4) */
#define LORA_READING_SIZE 5

/* --------------------------------------------------------------------------
 * RPC command IDs
 * -------------------------------------------------------------------------- */

/* Diagnostics (0x00-0x0F) */
#define LORA_RPC_PING        0x00
#define LORA_RPC_GET_VERSION 0x01
#define LORA_RPC_GET_STATUS  0x02

/* Configuration (0x10-0x1F) */
#define LORA_RPC_SET_PUBLISH_INTERVAL 0x10
#define LORA_RPC_SET_SPREADING        0x11
#define LORA_RPC_SET_TX_POWER         0x12
#define LORA_RPC_SET_KEEPALIVE        0x13
#define LORA_RPC_SET_CHANGE_THRESHOLD 0x14

/* Trigger (0x20-0x2F) */
#define LORA_RPC_TRIGGER_SAMPLE 0x20

/* FOTA (0x30-0x3F) */
#define LORA_RPC_FOTA_START  0x30
#define LORA_RPC_FOTA_CANCEL 0x31

/* System (0xF0-0xFF) */
#define LORA_RPC_REBOOT 0xFF

#ifdef __cplusplus
}
#endif

#endif /* LORA_PROTO_LORA_FRAME_H_ */
