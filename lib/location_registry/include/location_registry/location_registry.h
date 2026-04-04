/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file location_registry.h
 * @brief Runtime CRUD for named physical locations.
 *
 * Locations are independent entities (not DT-derived). Sensors are
 * assigned to locations at runtime by writing to sensor_registry_meta.
 *
 * Consumers (HTTP dashboard, shell) call location_registry_foreach()
 * to enumerate all locations without hardcoding names.
 */

#ifndef LOCATION_REGISTRY_LOCATION_REGISTRY_H_
#define LOCATION_REGISTRY_LOCATION_REGISTRY_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a named location to the registry.
 *
 * @param name Location name (NUL-terminated). Length must be ≤
 *             CONFIG_LOCATION_REGISTRY_NAME_LEN.
 * @return 0 on success.
 * @return -EINVAL if name is NULL or empty.
 * @return -ENAMETOOLONG if name exceeds CONFIG_LOCATION_REGISTRY_NAME_LEN.
 * @return -EEXIST if a location with this name already exists.
 * @return -ENOMEM if the registry is full.
 */
int location_registry_add(const char *name);

/**
 * @brief Remove a named location from the registry.
 *
 * @param name Location name to remove.
 * @return 0 on success.
 * @return -EINVAL if name is NULL or empty.
 * @return -ENOENT if no location with this name exists.
 */
int location_registry_remove(const char *name);

/**
 * @brief Check whether a named location exists.
 *
 * @param name Location name.
 * @return true if the location is registered, false otherwise.
 */
bool location_registry_exists(const char *name);

/**
 * @brief Return the number of registered locations.
 */
int location_registry_count(void);

/**
 * @brief Iterate over all registered locations.
 *
 * @param cb Callback invoked for each location name. Return non-zero to stop.
 * @param user_data Passed verbatim to the callback.
 */
void location_registry_foreach(int (*cb)(const char *name, void *user_data), void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_REGISTRY_LOCATION_REGISTRY_H_ */
