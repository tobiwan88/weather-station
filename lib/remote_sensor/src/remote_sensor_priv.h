/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file remote_sensor_priv.h
 * @brief Internal types shared between remote_sensor_manager.c and
 *        remote_sensor_settings.c.  Not part of the public API.
 */

#ifndef REMOTE_SENSOR_PRIV_H_
#define REMOTE_SENSOR_PRIV_H_

#include <stdbool.h>
#include <stdint.h>

#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>

/**
 * @brief Tracks a registered remote peer for trigger routing and persistence.
 *
 * Owned by remote_sensor_manager.c.  remote_sensor_settings.c accesses
 * it through the save/restore helpers declared below.
 */
struct remote_peer {
	uint32_t uid;
	enum remote_transport_proto proto;
	uint8_t peer_addr[REMOTE_SENSOR_ADDR_MAX_LEN];
	uint8_t peer_addr_len;
	bool used;
};

/**
 * @brief Save a newly-registered peer to persistent settings.
 *
 * Called by remote_sensor_manager after a successful do_register().
 * Implemented in remote_sensor_settings.c; compiled only when
 * CONFIG_REMOTE_SENSOR_PERSIST=y.
 */
void remote_sensor_settings_save(const struct remote_peer *peer, const char *label,
				 enum sensor_type type);

/**
 * @brief Re-register a peer that was loaded from settings on boot.
 *
 * Called by remote_sensor_settings.c during commit.
 * Implemented in remote_sensor_manager.c.
 */
int remote_sensor_manager_restore(uint32_t uid, enum remote_transport_proto proto,
				  const uint8_t *peer_addr, uint8_t peer_addr_len,
				  const char *label, enum sensor_type type);

#endif /* REMOTE_SENSOR_PRIV_H_ */
