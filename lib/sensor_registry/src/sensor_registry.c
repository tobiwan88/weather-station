/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <sensor_registry/sensor_registry.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct sensor_registry_entry *registry[SENSOR_REGISTRY_MAX_ENTRIES];
static int registry_count;

/* Mutex protecting concurrent access from multiple SYS_INIT callbacks. */
static K_MUTEX_DEFINE(registry_mutex);

int sensor_registry_register(const struct sensor_registry_entry *entry)
{
	if (entry == NULL || entry->label == NULL || entry->location == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&registry_mutex, K_FOREVER);

	/* Reject duplicate UIDs. */
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == entry->uid) {
			k_mutex_unlock(&registry_mutex);
			return -EEXIST;
		}
	}

	if (registry_count >= SENSOR_REGISTRY_MAX_ENTRIES) {
		k_mutex_unlock(&registry_mutex);
		return -ENOMEM;
	}

	registry[registry_count++] = entry;
	k_mutex_unlock(&registry_mutex);
	return 0;
}

const struct sensor_registry_entry *sensor_registry_lookup(uint32_t uid)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			const struct sensor_registry_entry *e = registry[i];

			k_mutex_unlock(&registry_mutex);
			return e;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return NULL;
}

void sensor_registry_foreach(int (*cb)(const struct sensor_registry_entry *e, void *user_data),
			     void *user_data)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		int rc = cb(registry[i], user_data);

		if (rc != 0) {
			break;
		}
	}
	k_mutex_unlock(&registry_mutex);
}

int sensor_registry_count(void)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	int count = registry_count;

	k_mutex_unlock(&registry_mutex);
	return count;
}

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
#include <string.h>

static char user_desc[SENSOR_REGISTRY_MAX_ENTRIES]
		     [CONFIG_SENSOR_REGISTRY_USER_META_MAX_LEN + 1];

int sensor_registry_set_description(uint32_t uid, const char *desc)
{
	if (desc == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			strncpy(user_desc[i], desc, CONFIG_SENSOR_REGISTRY_USER_META_MAX_LEN);
			user_desc[i][CONFIG_SENSOR_REGISTRY_USER_META_MAX_LEN] = '\0';
			k_mutex_unlock(&registry_mutex);
			return 0;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return -ENOENT;
}

const char *sensor_registry_get_description(uint32_t uid)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			const char *d = user_desc[i];

			k_mutex_unlock(&registry_mutex);
			return d;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return NULL;
}
#endif /* CONFIG_SENSOR_REGISTRY_USER_META */
