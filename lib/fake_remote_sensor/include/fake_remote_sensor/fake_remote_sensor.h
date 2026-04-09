/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_remote_sensor.h
 * @brief Public API for the fake remote sensor testing stub.
 *
 * The stub simulates CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT remote sensor nodes,
 * each advertising temperature, humidity, CO₂, and VOC (four UIDs per node).
 *
 * Controlled via the "fake_remote" shell command group or by calling the
 * functions below directly from test code.
 */

#ifndef FAKE_REMOTE_SENSOR_FAKE_REMOTE_SENSOR_H_
#define FAKE_REMOTE_SENSOR_FAKE_REMOTE_SENSOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Announce fake discovery events for all simulated nodes.
 *
 * Publishes one REMOTE_DISCOVERY_FOUND event per (node × sensor_type) pair
 * on remote_discovery_chan.  The remote_sensor_manager will register each
 * sensor in sensor_registry (if CONFIG_REMOTE_SENSOR_AUTO_REGISTER=y).
 *
 * Safe to call multiple times — the manager deduplicates by UID.
 *
 * @return 0 on success, negative errno if a channel publish fails.
 */
int fake_remote_sensor_announce(void);

/**
 * @brief Publish one synthetic measurement from all registered fake nodes.
 *
 * For each registered node, emits env_sensor_data events for temperature,
 * humidity, CO₂, and VOC on sensor_event_chan.  Useful for manual triggering from
 * test code or the shell.
 *
 * @return 0 on success, first non-zero errno encountered on failure.
 */
int fake_remote_sensor_publish_all(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_REMOTE_SENSOR_FAKE_REMOTE_SENSOR_H_ */
