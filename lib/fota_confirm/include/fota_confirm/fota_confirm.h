/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Transport vtable for sensor-node firmware update confirmation.
 *
 * Implement one instance per wireless transport (LoRa, BLE, …) using
 * this interface. Implementation is deferred — see ADR-014.
 *
 * @note The gateway calls confirm() to verify that the sensor node has
 *       successfully applied the firmware update and is ready to run.
 */
struct fota_confirm_transport_api {
	/**
	 * @brief Confirm firmware update completion on a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to confirm.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*confirm)(uint32_t target_uid);

	/**
	 * @brief Query the update status on a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to query.
	 * @param status      Output buffer for status string (e.g. "applied", "pending").
	 * @param len         Size of buf including NUL terminator.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*get_status)(uint32_t target_uid, char *status, size_t len);
};
