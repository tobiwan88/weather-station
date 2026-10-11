/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_PROTO_LORA_PACKET_H_
#define LORA_PROTO_LORA_PACKET_H_

#include <stddef.h>
#include <stdint.h>

#include <lora_proto/lora_frame.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Header (10) + GCM tag (12) + CRC16 (2): bytes added to every encrypted frame. */
#define LORA_PROTO_OVERHEAD LORA_OVERHEAD

/** Number of 5-byte readings that fit in a packet of @p sf_max_packet bytes. */
#define LORA_PROTO_MAX_READINGS(sf_max_packet)                                                     \
	(((sf_max_packet)-LORA_PROTO_OVERHEAD) / LORA_READING_SIZE)

BUILD_ASSERT(LORA_PROTO_MAX_READINGS(LORA_MAX_PACKET_SF12) == 5,
	     "an SF12 packet must carry exactly 5 readings");

/**
 * @brief Encode a LoRa frame: header + (encrypted) payload + CRC16.
 *
 * With a key the frame is AES-128-GCM protected (12-byte tag, header as AAD) and
 * LORA_FLAG_ENCRYPTED is set in the emitted header. The nonce is built here from
 * the header (node id of the non-gateway end, direction, 32-bit seq); callers
 * only choose a unique seq per node and direction. The CRC16 covers header,
 * payload and tag. PSA calls are serialized internally, the codec is thread-safe.
 *
 * @param hdr       Header to emit (not modified; the ENCRYPTED flag is added on the wire).
 * @param payload   Frame-type-specific payload.
 * @param len       Payload length in bytes.
 * @param key       16-byte AES key, or NULL for a plaintext frame.
 * @param out       Output buffer.
 * @param out_size  Size of @p out (e.g. 51 for SF12).
 * @param out_len   Output: encoded frame length.
 * @retval 0 on success
 * @retval -EINVAL on bad arguments
 * @retval -EMSGSIZE if the frame does not fit in @p out_size
 * @retval -EIO on crypto failure
 */
int lora_packet_encode(const struct lora_l2_header *hdr, const void *payload, size_t len,
		       const uint8_t *key, uint8_t *out, size_t out_size, size_t *out_len);

/**
 * @brief Decode a LoRa frame: verify CRC16, then authenticate and decrypt.
 *
 * @param in           Frame bytes as received.
 * @param in_len       Frame length.
 * @param key          16-byte AES key of the peer node, or NULL to accept only
 *                     plaintext frames. With a key, a frame without
 *                     LORA_FLAG_ENCRYPTED is rejected (no silent downgrade).
 * @param hdr          Output: decoded header.
 * @param payload      Output buffer for the payload.
 * @param payload_size Size of @p payload.
 * @param payload_len  Output: payload length.
 * @retval 0 on success
 * @retval -EINVAL on bad arguments or a truncated frame
 * @retval -EBADMSG on CRC or authentication failure
 * @retval -EPERM if the frame is encrypted but @p key is NULL, or a key was
 *         given but the frame is not encrypted
 * @retval -EMSGSIZE if @p payload is too small
 */
int lora_packet_decode(const uint8_t *in, size_t in_len, const uint8_t *key,
		       struct lora_l2_header *hdr, uint8_t *payload, size_t payload_size,
		       size_t *payload_len);

#ifdef __cplusplus
}
#endif

#endif /* LORA_PROTO_LORA_PACKET_H_ */
