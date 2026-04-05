/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sensor_registry/sensor_registry.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_SENSOR_REGISTRY_SETTINGS
#	include <zephyr/settings/settings.h>
#endif

static const struct sensor_registry_entry *registry[SENSOR_REGISTRY_MAX_ENTRIES];
static int registry_count;

/* Mutex protecting concurrent access from multiple SYS_INIT callbacks. */
static K_MUTEX_DEFINE(registry_mutex);

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
static struct sensor_registry_meta meta[SENSOR_REGISTRY_MAX_ENTRIES];
#endif

int sensor_registry_register(const struct sensor_registry_entry *entry)
{
	if (entry == NULL || entry->label == NULL) {
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

	int idx = registry_count;

	registry[idx] = entry;

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
	/* Pre-seed display name from DT label; location starts empty. */
	strncpy(meta[idx].display_name, entry->label, CONFIG_SENSOR_REGISTRY_META_NAME_LEN);
	meta[idx].display_name[CONFIG_SENSOR_REGISTRY_META_NAME_LEN] = '\0';

	meta[idx].location[0] = '\0';
	meta[idx].description[0] = '\0';
	meta[idx].enabled = true;
#endif

	registry_count++;
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

int sensor_registry_set_meta(uint32_t uid, const struct sensor_registry_meta *m)
{
	if (m == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid != uid) {
			continue;
		}
		meta[i] = *m;
		k_mutex_unlock(&registry_mutex);

#	ifdef CONFIG_SENSOR_REGISTRY_SETTINGS
		char key[32];

		snprintf(key, sizeof(key), "sreg/%08" PRIx32 "/name", uid);
		settings_save_one(key, m->display_name, strlen(m->display_name) + 1);

		snprintf(key, sizeof(key), "sreg/%08" PRIx32 "/loc", uid);
		settings_save_one(key, m->location, strlen(m->location) + 1);

		snprintf(key, sizeof(key), "sreg/%08" PRIx32 "/desc", uid);
		settings_save_one(key, m->description, strlen(m->description) + 1);

		snprintf(key, sizeof(key), "sreg/%08" PRIx32 "/en", uid);
		uint8_t en = m->enabled ? 1u : 0u;

		settings_save_one(key, &en, sizeof(en));
#	endif
		return 0;
	}
	k_mutex_unlock(&registry_mutex);
	return -ENOENT;
}

int sensor_registry_get_meta(uint32_t uid, struct sensor_registry_meta *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			*out = meta[i];
			k_mutex_unlock(&registry_mutex);
			return 0;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return -ENOENT;
}

const char *sensor_registry_get_display_name(uint32_t uid)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			const char *name = (meta[i].display_name[0] != '\0') ? meta[i].display_name
									     : registry[i]->label;

			k_mutex_unlock(&registry_mutex);
			return name;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return NULL;
}

const char *sensor_registry_get_location(uint32_t uid)
{
	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid == uid) {
			const char *loc = meta[i].location;

			k_mutex_unlock(&registry_mutex);
			return loc;
		}
	}
	k_mutex_unlock(&registry_mutex);
	return NULL;
}

#	ifdef CONFIG_SENSOR_REGISTRY_SETTINGS

static int sreg_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);

	/* key = "<uid_hex>/<field>", e.g. "00000001/name" */
	const char *slash = strchr(key, '/');

	if (!slash || slash == key) {
		return -EINVAL;
	}

	size_t uid_len = (size_t)(slash - key);

	if (uid_len > 8) {
		return -EINVAL;
	}

	char uid_str[9];

	memcpy(uid_str, key, uid_len);
	uid_str[uid_len] = '\0';

	uint32_t uid = (uint32_t)strtoul(uid_str, NULL, 16);
	const char *field = slash + 1;

	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < registry_count; i++) {
		if (registry[i]->uid != uid) {
			continue;
		}

		if (strcmp(field, "name") == 0) {
			char buf[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];
			ssize_t ret = read_cb(cb_arg, buf, sizeof(buf) - 1);

			if (ret > 0) {
				buf[ret] = '\0';
				strncpy(meta[i].display_name, buf,
					CONFIG_SENSOR_REGISTRY_META_NAME_LEN);
				meta[i].display_name[CONFIG_SENSOR_REGISTRY_META_NAME_LEN] = '\0';
			}
		} else if (strcmp(field, "loc") == 0) {
			char buf[CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN + 1];
			ssize_t ret = read_cb(cb_arg, buf, sizeof(buf) - 1);

			if (ret > 0) {
				buf[ret] = '\0';
				strncpy(meta[i].location, buf,
					CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN);
				meta[i].location[CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN] = '\0';
			}
		} else if (strcmp(field, "desc") == 0) {
			char buf[CONFIG_SENSOR_REGISTRY_META_DESC_LEN + 1];
			ssize_t ret = read_cb(cb_arg, buf, sizeof(buf) - 1);

			if (ret > 0) {
				buf[ret] = '\0';
				strncpy(meta[i].description, buf,
					CONFIG_SENSOR_REGISTRY_META_DESC_LEN);
				meta[i].description[CONFIG_SENSOR_REGISTRY_META_DESC_LEN] = '\0';
			}
		} else if (strcmp(field, "en") == 0) {
			uint8_t en = 1;

			read_cb(cb_arg, &en, sizeof(en));
			meta[i].enabled = (en != 0);
		}
		break;
	}

	k_mutex_unlock(&registry_mutex);
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(sreg, "sreg", NULL, sreg_settings_set, NULL, NULL);

/* Load saved metadata after all sensor drivers have registered (priority 90/91). */
static int sensor_registry_settings_load(void)
{
	return settings_load_subtree("sreg");
}

SYS_INIT(sensor_registry_settings_load, APPLICATION, 95);

#	endif /* CONFIG_SENSOR_REGISTRY_SETTINGS */
#endif         /* CONFIG_SENSOR_REGISTRY_USER_META */
