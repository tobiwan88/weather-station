/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sensor_registry.h
 * @brief Sensor registry: maps sensor_uid to human-readable metadata.
 *
 * Consumers (display, MQTT, shell) look up the registry by uid to
 * obtain a label and location string without hardcoding UIDs.
 *
 * Sensor drivers call sensor_registry_register() during SYS_INIT at
 * APPLICATION priority, before the main application thread starts.
 */

#ifndef SENSOR_REGISTRY_SENSOR_REGISTRY_H_
#define SENSOR_REGISTRY_SENSOR_REGISTRY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of sensors that can be registered. */
#define SENSOR_REGISTRY_MAX_ENTRIES 16

/** Metadata record for one sensor. */
struct sensor_registry_entry {
	uint32_t uid;         /**< Unique sensor identifier (from DT)   */
	const char *label;    /**< Human-readable name, e.g. "indoor"   */
	const char *location; /**< Physical location, e.g. "living_room" */
	bool is_remote;       /**< true if sensor lives on a remote node */
};

/**
 * @brief Register a sensor in the global registry.
 *
 * Must be called during SYS_INIT at APPLICATION priority or later.
 *
 * @param entry Pointer to a statically-allocated entry. The registry
 *              stores the pointer directly — do not free the memory.
 * @return 0 on success, -ENOMEM if the registry is full, -EINVAL on bad
 *         arguments, -EEXIST if the uid is already registered.
 */
int sensor_registry_register(const struct sensor_registry_entry *entry);

/**
 * @brief Look up a sensor by uid.
 *
 * @param uid Sensor uid to search for.
 * @return Pointer to the entry, or NULL if not found.
 */
const struct sensor_registry_entry *sensor_registry_lookup(uint32_t uid);

/**
 * @brief Iterate over all registered sensors.
 *
 * @param cb Callback invoked for each entry. Return non-zero to stop.
 * @param user_data Passed verbatim to the callback.
 */
void sensor_registry_foreach(int (*cb)(const struct sensor_registry_entry *e, void *user_data),
			     void *user_data);

/**
 * @brief Return the number of registered sensors.
 */
int sensor_registry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_REGISTRY_SENSOR_REGISTRY_H_ */
