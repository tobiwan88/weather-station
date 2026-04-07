/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <location_registry/location_registry.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS
#	include <zephyr/settings/settings.h>
#endif

LOG_MODULE_REGISTER(location_registry, LOG_LEVEL_INF);

static char locations[CONFIG_LOCATION_REGISTRY_MAX_LOCATIONS]
		     [CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
static int location_count;

static K_MUTEX_DEFINE(loc_mutex);

int location_registry_add(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return -EINVAL;
	}

	if (strlen(name) > CONFIG_LOCATION_REGISTRY_NAME_LEN) {
		return -ENAMETOOLONG;
	}

	k_mutex_lock(&loc_mutex, K_FOREVER);

	for (int i = 0; i < location_count; i++) {
		if (strcmp(locations[i], name) == 0) {
			k_mutex_unlock(&loc_mutex);
			return -EEXIST;
		}
	}

	if (location_count >= CONFIG_LOCATION_REGISTRY_MAX_LOCATIONS) {
		k_mutex_unlock(&loc_mutex);
		return -ENOMEM;
	}

	strncpy(locations[location_count], name, CONFIG_LOCATION_REGISTRY_NAME_LEN);
	locations[location_count][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';
	location_count++;

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS
	/* Snapshot the count while the lock is still held. */
	int snap_count = location_count;
#endif

	k_mutex_unlock(&loc_mutex);

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS
	/* Persist the full location list using the snapshotted count. */
	char key[32];

	for (int i = 0; i < snap_count; i++) {
		snprintf(key, sizeof(key), "loc/%d", i);
		settings_save_one(key, locations[i], strlen(locations[i]) + 1);
	}
#endif

	LOG_INF("Added location: %s", name);
	return 0;
}

int location_registry_remove(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return -EINVAL;
	}

	k_mutex_lock(&loc_mutex, K_FOREVER);

	int found = -1;

	for (int i = 0; i < location_count; i++) {
		if (strcmp(locations[i], name) == 0) {
			found = i;
			break;
		}
	}

	if (found < 0) {
		k_mutex_unlock(&loc_mutex);
		return -ENOENT;
	}

	/* Shift remaining entries down. */
	for (int i = found; i < location_count - 1; i++) {
		strncpy(locations[i], locations[i + 1], CONFIG_LOCATION_REGISTRY_NAME_LEN);
		locations[i][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';
	}
	locations[location_count - 1][0] = '\0';
	location_count--;

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS
	/* Snapshot the count while the lock is still held. */
	int snap_count = location_count;
#endif

	k_mutex_unlock(&loc_mutex);

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS
	/* Rewrite all slots; clear the now-vacated tail slot. */
	char key[32];

	for (int i = 0; i < snap_count; i++) {
		snprintf(key, sizeof(key), "loc/%d", i);
		settings_save_one(key, locations[i], strlen(locations[i]) + 1);
	}
	/* Clear the slot that was freed. */
	snprintf(key, sizeof(key), "loc/%d", snap_count);
	settings_save_one(key, "", 1);
#endif

	LOG_INF("Removed location: %s", name);
	return 0;
}

bool location_registry_exists(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return false;
	}

	k_mutex_lock(&loc_mutex, K_FOREVER);
	for (int i = 0; i < location_count; i++) {
		if (strcmp(locations[i], name) == 0) {
			k_mutex_unlock(&loc_mutex);
			return true;
		}
	}
	k_mutex_unlock(&loc_mutex);
	return false;
}

int location_registry_count(void)
{
	k_mutex_lock(&loc_mutex, K_FOREVER);
	int count = location_count;

	k_mutex_unlock(&loc_mutex);
	return count;
}

void location_registry_foreach(int (*cb)(const char *name, void *user_data), void *user_data)
{
	k_mutex_lock(&loc_mutex, K_FOREVER);
	for (int i = 0; i < location_count; i++) {
		int rc = cb(locations[i], user_data);

		if (rc != 0) {
			break;
		}
	}
	k_mutex_unlock(&loc_mutex);
}

#ifdef CONFIG_LOCATION_REGISTRY_SETTINGS

static int loc_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);

	/* key = "<index>", e.g. "3" */
	char *endptr;
	long idx = strtol(key, &endptr, 10);

	if (endptr == key || *endptr != '\0' || idx < 0 ||
	    idx >= CONFIG_LOCATION_REGISTRY_MAX_LOCATIONS) {
		return -EINVAL;
	}

	char buf[CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
	ssize_t ret = read_cb(cb_arg, buf, sizeof(buf) - 1);

	if (ret <= 0 || buf[0] == '\0') {
		return 0;
	}
	buf[ret] = '\0';

	/* Restore into the locations array directly (settings load is
	 * single-threaded at init time, mutex not required here). */
	if (idx >= location_count) {
		/* Fill any gap with empty strings first. */
		for (int i = location_count; i < (int)idx; i++) {
			locations[i][0] = '\0';
		}
		location_count = (int)idx + 1;
	}
	strncpy(locations[idx], buf, CONFIG_LOCATION_REGISTRY_NAME_LEN);
	locations[idx][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(loc, "loc", NULL, loc_settings_set, NULL, NULL);

static int location_registry_settings_load(void)
{
	return settings_load_subtree("loc");
}

SYS_INIT(location_registry_settings_load, APPLICATION, 96);

#endif /* CONFIG_LOCATION_REGISTRY_SETTINGS */
