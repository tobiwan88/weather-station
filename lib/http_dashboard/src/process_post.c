/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include <config_cmd/config_cmd.h>
#include <location_registry/location_registry.h>
#include <sensor_registry/sensor_registry.h>
#include <zephyr/zbus/zbus.h>

#include "form_parse.h"
#include "process_post.h"

LOG_MODULE_DECLARE(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

/* Runtime-configurable values (last values POSTed via /api/config). */
static char sntp_server_buf[64];
static uint32_t trigger_interval_ms;
static struct k_spinlock sntp_lock;

uint32_t config_state_get_trigger_ms(void)
{
	return trigger_interval_ms;
}

void config_state_copy_sntp_server(char *out, size_t len)
{
	__ASSERT(len > 0, "out buffer length must be > 0");
	k_spinlock_key_t key = k_spin_lock(&sntp_lock);

	strncpy(out, sntp_server_buf, len - 1);
	out[len - 1] = '\0';
	k_spin_unlock(&sntp_lock, key);
}

void process_post(const uint8_t *body, size_t len)
{
	LOG_DBG("process_post: body %zu B", len);
	/* Buffer must hold the full accumulated POST body (1024 B max). */
	static char buf[1025];
	size_t copy_len = MIN(len, sizeof(buf) - 1);

	memcpy(buf, body, copy_len);
	buf[copy_len] = '\0';

	/* Pre-extract loc_name before the loop mutates buf. */
	char loc_name_pre[CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
	bool has_loc_name = form_extract(buf, "loc_name", loc_name_pre, sizeof(loc_name_pre));

	char *token = buf;

	while (token != NULL && *token != '\0') {
		char *amp = strchr(token, '&');

		if (amp) {
			*amp = '\0';
		}

		char *eq = strchr(token, '=');

		if (eq) {
			*eq = '\0';
			const char *key = token;
			char *val = eq + 1;

			url_decode(val);

			if (strcmp(key, "trigger_interval_ms") == 0) {
				errno = 0;
				unsigned long ms_ul = strtoul(val, NULL, 10);

				if (errno == ERANGE || ms_ul > UINT32_MAX) {
					LOG_WRN("trigger_interval_ms out of range, ignoring");
					token = amp ? (amp + 1) : NULL;
					continue;
				}
				uint32_t ms = (uint32_t)ms_ul;
				struct config_cmd_event cmd = {
					.cmd = CONFIG_CMD_SET_TRIGGER_INTERVAL,
					.arg = ms,
				};

				trigger_interval_ms = ms;
				int rc = zbus_chan_pub(&config_cmd_chan, &cmd, K_NO_WAIT);

				if (rc != 0) {
					LOG_WRN("config_cmd_chan pub failed: %d", rc);
				}
				LOG_INF("trigger interval set to %u ms", ms);
			} else if (strcmp(key, "sntp_server") == 0) {
				k_spinlock_key_t skey = k_spin_lock(&sntp_lock);

				strncpy(sntp_server_buf, val, sizeof(sntp_server_buf) - 1);
				sntp_server_buf[sizeof(sntp_server_buf) - 1] = '\0';
				k_spin_unlock(&sntp_lock, skey);
				LOG_INF("sntp server set to %s", val);
			} else if (strcmp(key, "action") == 0 && strcmp(val, "sntp_resync") == 0) {
				struct config_cmd_event cmd = {
					.cmd = CONFIG_CMD_SNTP_RESYNC,
					.arg = 0,
				};
				int rc = zbus_chan_pub(&config_cmd_chan, &cmd, K_NO_WAIT);

				if (rc != 0) {
					LOG_WRN("config_cmd_chan pub failed: %d", rc);
				}
				LOG_INF("SNTP resync triggered");
			} else if (strcmp(key, "action") == 0 &&
				   (strcmp(val, "add_location") == 0 ||
				    strcmp(val, "remove_location") == 0)) {
				if (has_loc_name && loc_name_pre[0] != '\0') {
					if (strcmp(val, "add_location") == 0) {
						int rc = location_registry_add(loc_name_pre);

						if (rc != 0 && rc != -EEXIST) {
							LOG_WRN("location add '%s' failed: %d",
								loc_name_pre, rc);
						} else {
							LOG_INF("Location added: %s", loc_name_pre);
						}
					} else {
						int rc = location_registry_remove(loc_name_pre);

						if (rc != 0) {
							LOG_WRN("location remove '%s' failed: %d",
								loc_name_pre, rc);
						} else {
							LOG_INF("Location removed: %s",
								loc_name_pre);
						}
					}
				}
#ifdef CONFIG_SENSOR_REGISTRY_USER_META
			} else if (strncmp(key, "sensor_", 7) == 0) {
				/* sensor_<uid>_<field>: name, loc, desc, en */
				char tmp[64];

				strncpy(tmp, key + 7, sizeof(tmp) - 1);
				tmp[sizeof(tmp) - 1] = '\0';

				char *last_us = strrchr(tmp, '_');

				if (last_us && last_us != tmp) {
					*last_us = '\0';
					errno = 0;
					unsigned long uid_ul = strtoul(tmp, NULL, 10);

					if (errno == ERANGE || uid_ul > UINT32_MAX) {
						token = amp ? (amp + 1) : NULL;
						continue;
					}
					uint32_t uid = (uint32_t)uid_ul;
					const char *field = last_us + 1;
					struct sensor_registry_meta m;

					if (sensor_registry_get_meta(uid, &m) == 0) {
						if (strcmp(field, "name") == 0) {
							strncpy(m.display_name, val,
								CONFIG_SENSOR_REGISTRY_META_NAME_LEN);
							m.display_name
								[CONFIG_SENSOR_REGISTRY_META_NAME_LEN] =
								'\0';
						} else if (strcmp(field, "loc") == 0) {
							strncpy(m.location, val,
								CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN);
							m.location
								[CONFIG_SENSOR_REGISTRY_META_LOCATION_LEN] =
								'\0';
						} else if (strcmp(field, "desc") == 0) {
							strncpy(m.description, val,
								CONFIG_SENSOR_REGISTRY_META_DESC_LEN);
							m.description
								[CONFIG_SENSOR_REGISTRY_META_DESC_LEN] =
								'\0';
						} else if (strcmp(field, "en") == 0) {
							m.enabled = (strcmp(val, "1") == 0);
						}
						sensor_registry_set_meta(uid, &m);
						LOG_DBG("sensor %u %s=\"%s\"", uid, field, val);
					}
				}
#endif /* CONFIG_SENSOR_REGISTRY_USER_META */
			}
		}

		token = amp ? (amp + 1) : NULL;
	}
}
