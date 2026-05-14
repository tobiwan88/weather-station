/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Transport vtable for gateway-to-sensor-node firmware relay.
 *
 * Implement one instance per wireless transport (LoRa, BLE, …) using
 * this interface. Implementation is deferred — see ADR-014.
 *
 * @note The gateway calls upload() in chunks; the transport is responsible
 *       for fragmentation, duty-cycle management, and ACK/retry.
 */
struct fota_relay_transport_api {
	/**
	 * @brief Upload a firmware chunk to a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to update.
	 * @param data        Pointer to image chunk (may be NULL when len==0).
	 * @param len         Length of this chunk in bytes.
	 * @param offset      Byte offset of this chunk in the full image.
	 * @param last        True if this is the final chunk.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*upload)(uint32_t target_uid, const uint8_t *data, size_t len, size_t offset,
		      bool last);

	/**
	 * @brief Query the running firmware version on a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to query.
	 * @param buf         Output buffer for version string (e.g. "1.2.3+0").
	 * @param len         Size of buf including NUL terminator.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*get_version)(uint32_t target_uid, char *buf, size_t len);
};
