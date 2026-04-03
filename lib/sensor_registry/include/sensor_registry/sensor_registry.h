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

/** Compile-time metadata record for one sensor (populated from DT, immutable). */
struct sensor_registry_entry {
	uint32_t uid;         /**< Unique sensor identifier (from DT)    */
	const char *label;    /**< DT node name, e.g. "fake-temp-indoor" */
	const char *location; /**< DT location property, e.g. "living_room" */
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

#ifdef CONFIG_SENSOR_REGISTRY_USER_META

/**
 * @brief User-editable runtime metadata for a sensor.
 *
 * Pre-seeded from DT defaults on sensor_registry_register(). All fields
 * can be updated at runtime (e.g. via the HTTP dashboard).
 *
 * To add new fields in the future: extend this struct, add a settings key
 * handler case in sensor_registry.c, and extend the dashboard JSON.
 */
struct sensor_registry_meta {
	/** User-friendly display name (defaults to DT label). */
	char display_name[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];
	/** Location override (defaults to DT location property). */
	char location[CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN + 1];
	/** Free-text description / notes. */
	char description[CONFIG_SENSOR_REGISTRY_META_DESC_LEN + 1];
	/** When false the sensor is excluded from dashboard output. */
	bool enabled;
};

/**
 * @brief Write user metadata for a registered sensor.
 *
 * @param uid  Sensor uid (must already be registered).
 * @param meta Metadata to copy in. Must not be NULL.
 * @return 0, -ENOENT if uid not found, -EINVAL if meta is NULL.
 */
int sensor_registry_set_meta(uint32_t uid, const struct sensor_registry_meta *meta);

/**
 * @brief Read user metadata for a registered sensor.
 *
 * @param uid Sensor uid.
 * @param out Buffer to copy metadata into. Must not be NULL.
 * @return 0, -ENOENT if uid not found, -EINVAL if out is NULL.
 */
int sensor_registry_get_meta(uint32_t uid, struct sensor_registry_meta *out);

/**
 * @brief Return the display name for a sensor.
 *
 * Returns meta.display_name if non-empty, otherwise falls back to the
 * DT label from sensor_registry_entry.
 *
 * @param uid Sensor uid.
 * @return Display name string, or NULL if uid is not registered.
 */
const char *sensor_registry_get_display_name(uint32_t uid);

/**
 * @brief Return the effective location string for a sensor.
 *
 * Returns meta.location if non-empty, otherwise falls back to the DT
 * location from sensor_registry_entry.
 *
 * @param uid Sensor uid.
 * @return Location string, or NULL if uid is not registered.
 */
const char *sensor_registry_get_location(uint32_t uid);

#endif /* CONFIG_SENSOR_REGISTRY_USER_META */

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_REGISTRY_SENSOR_REGISTRY_H_ */
