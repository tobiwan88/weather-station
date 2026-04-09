/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file http_dashboard.c
 * @brief HTTP weather dashboard: live Chart.js graph + configuration page.
 *
 * Architecture:
 *   - ZBUS_LISTENER_DEFINE subscribes to sensor_event_chan (priority 97).
 *   - Per-sensor ring buffers store the last HISTORY_SIZE samples.
 *   - k_spinlock protects ring buffers (callback may run from ISR/timer context).
 *   - HTTP_SERVICE_DEFINE + five HTTP_RESOURCE_DEFINE handle the five URLs.
 *   - SYS_INIT at APPLICATION 97 registers the zbus observer and starts the
 *     HTTP server.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/zbus/zbus.h>

#include <config_cmd/config_cmd.h>
#include <location_registry/location_registry.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>

LOG_MODULE_REGISTER(http_dashboard, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* Ring-buffer data model                                                       */
/* -------------------------------------------------------------------------- */

struct sensor_sample {
	int64_t timestamp_ms;
	int32_t q31_value;
};

struct sensor_history {
	bool valid;
	uint32_t uid;
	enum sensor_type type;
	struct sensor_sample samples[CONFIG_HTTP_DASHBOARD_HISTORY_SIZE];
	uint16_t head;  /* next write index */
	uint16_t count; /* number of valid samples (capped at HISTORY_SIZE) */
};

static struct sensor_history histories[CONFIG_HTTP_DASHBOARD_MAX_SENSORS];
static struct k_spinlock history_lock;

/* Snapshot taken under spinlock, serialised outside — one HTTP req at a time. */
static struct sensor_history snap[CONFIG_HTTP_DASHBOARD_MAX_SENSORS];

/* Runtime configurable values (last values POSTed via /api/config). */
static char sntp_server_buf[64];
static uint32_t trigger_interval_ms;

/* -------------------------------------------------------------------------- */
/* zbus listener                                                               */
/* -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	k_spinlock_key_t key = k_spin_lock(&history_lock);

	int slot = -1;

	for (int i = 0; i < CONFIG_HTTP_DASHBOARD_MAX_SENSORS; i++) {
		if (histories[i].valid && histories[i].uid == evt->sensor_uid &&
		    histories[i].type == evt->type) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		for (int i = 0; i < CONFIG_HTTP_DASHBOARD_MAX_SENSORS; i++) {
			if (!histories[i].valid) {
				slot = i;
				histories[i].valid = true;
				histories[i].uid = evt->sensor_uid;
				histories[i].type = evt->type;
				histories[i].head = 0;
				histories[i].count = 0;
				break;
			}
		}
	}

	if (slot >= 0) {
		struct sensor_history *h = &histories[slot];

		h->samples[h->head].timestamp_ms = evt->timestamp_ms;
		h->samples[h->head].q31_value = evt->q31_value;
		h->head = (h->head + 1) % CONFIG_HTTP_DASHBOARD_HISTORY_SIZE;
		if (h->count < CONFIG_HTTP_DASHBOARD_HISTORY_SIZE) {
			h->count++;
		}
	}

	k_spin_unlock(&history_lock, key);
}

ZBUS_LISTENER_DEFINE(http_dashboard_listener, sensor_event_cb);

/* -------------------------------------------------------------------------- */
/* HTML page content                                                           */
/* -------------------------------------------------------------------------- */

static const char dashboard_html[] = {
#include "dashboard.html.inc"
	'\0'};

static const char config_html[] = {
#include "config.html.inc"
	'\0'};

/* -------------------------------------------------------------------------- */
/* Content-type headers                                                        */
/* -------------------------------------------------------------------------- */

static const struct http_header html_ct_hdr[] = {
	{.name = "Content-Type", .value = "text/html; charset=utf-8"},
};

static const struct http_header json_ct_hdr[] = {
	{.name = "Content-Type", .value = "application/json"},
};

static const struct http_header redir_hdrs[] = {
	{.name = "Location", .value = "/config"},
	{.name = "Content-Length", .value = "0"},
};

/* -------------------------------------------------------------------------- */
/* GET / — dashboard page                                                      */
/* -------------------------------------------------------------------------- */

static int root_handler(struct http_client_ctx *client, enum http_data_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_DATA_FINAL) {
		response_ctx->status = HTTP_200_OK;
		response_ctx->headers = html_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(html_ct_hdr);
		response_ctx->body = (const uint8_t *)dashboard_html;
		response_ctx->body_len = sizeof(dashboard_html) - 1;
		response_ctx->final_chunk = true;
	}
	return 0;
}

static struct http_resource_detail_dynamic root_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = root_handler,
};

/* -------------------------------------------------------------------------- */
/* GET /config — configuration page                                           */
/* -------------------------------------------------------------------------- */

static int config_page_handler(struct http_client_ctx *client, enum http_data_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_DATA_FINAL) {
		response_ctx->status = HTTP_200_OK;
		response_ctx->headers = html_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(html_ct_hdr);
		response_ctx->body = (const uint8_t *)config_html;
		response_ctx->body_len = sizeof(config_html) - 1;
		response_ctx->final_chunk = true;
	}
	return 0;
}

static struct http_resource_detail_dynamic config_page_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = config_page_handler,
};

/* -------------------------------------------------------------------------- */
/* GET /api/data — JSON sensor history                                        */
/* -------------------------------------------------------------------------- */

static const char *sensor_type_str(enum sensor_type t)
{
	switch (t) {
	case SENSOR_TYPE_TEMPERATURE:
		return "temperature";
	case SENSOR_TYPE_HUMIDITY:
		return "humidity";
	case SENSOR_TYPE_PRESSURE:
		return "pressure";
	case SENSOR_TYPE_CO2:
		return "co2";
	case SENSOR_TYPE_VOC:
		return "voc";
	case SENSOR_TYPE_LIGHT:
		return "light";
	case SENSOR_TYPE_UV_INDEX:
		return "uv_index";
	case SENSOR_TYPE_BATTERY_MV:
		return "battery_mv";
	default:
		return "unknown";
	}
}

/* Static JSON buffer — protected by resource holder (one request at a time). */
static uint8_t json_buf[8192];

static int api_data_handler(struct http_client_ctx *client, enum http_data_status status,
			    const struct http_request_ctx *request_ctx,
			    struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	/* Snapshot ring buffers under spinlock. */
	k_spinlock_key_t key = k_spin_lock(&history_lock);

	memcpy(snap, histories, sizeof(histories));
	k_spin_unlock(&history_lock, key);

	/* Serialise snapshot to json_buf. */
	int pos = 0;
	int rem = (int)sizeof(json_buf) - 1;

#define JAPPEND(fmt, ...)                                                                          \
	do {                                                                                       \
		int _jn = snprintf((char *)json_buf + pos, (size_t)(rem + 1), fmt, ##__VA_ARGS__); \
		if (_jn > 0) {                                                                     \
			pos += MIN(_jn, rem);                                                      \
			rem -= MIN(_jn, rem);                                                      \
		}                                                                                  \
	} while (0)

	JAPPEND("{\"sensors\":[");

	bool first_sensor = true;

	for (int i = 0; i < CONFIG_HTTP_DASHBOARD_MAX_SENSORS; i++) {
		if (!snap[i].valid || snap[i].count == 0) {
			continue;
		}

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
		/* Skip sensors the user has disabled. */
		struct sensor_registry_meta smeta = {0};

		if (sensor_registry_get_meta(snap[i].uid, &smeta) == 0 && !smeta.enabled) {
			continue;
		}
		const char *label = sensor_registry_get_display_name(snap[i].uid);
		const char *location = sensor_registry_get_location(snap[i].uid);

		if (!label) {
			label = "unknown";
		}
		if (!location) {
			location = "unknown";
		}
		const char *description = smeta.description;
#else
		const struct sensor_registry_entry *reg = sensor_registry_lookup(snap[i].uid);
		const char *label = reg ? reg->label : "unknown";
		const char *location = "";
#endif

		if (!first_sensor) {
			JAPPEND(",");
		}
		first_sensor = false;

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
		JAPPEND("{\"uid\":%u,\"label\":\"%s\",\"location\":\"%s\","
			"\"description\":\"%s\",\"type\":\"%s\",\"readings\":[",
			snap[i].uid, label, location, description, sensor_type_str(snap[i].type));
#else
		JAPPEND("{\"uid\":%u,\"label\":\"%s\",\"location\":\"%s\","
			"\"type\":\"%s\",\"readings\":[",
			snap[i].uid, label, location, sensor_type_str(snap[i].type));
#endif

		uint16_t start = (snap[i].head +
				  (uint16_t)(CONFIG_HTTP_DASHBOARD_HISTORY_SIZE - snap[i].count)) %
				 CONFIG_HTTP_DASHBOARD_HISTORY_SIZE;

		for (uint16_t j = 0; j < snap[i].count; j++) {
			uint16_t idx = (start + j) % CONFIG_HTTP_DASHBOARD_HISTORY_SIZE;

			if (j > 0) {
				JAPPEND(",");
			}
			double v = sensor_type_get_desc(snap[i].type)
					   ->decode_q31(snap[i].samples[idx].q31_value);

			JAPPEND("{\"t\":%" PRId64 ",\"v\":%.2f}", snap[i].samples[idx].timestamp_ms,
				v);
		}

		JAPPEND("]}");
	}

	JAPPEND("]}");

#undef JAPPEND

	json_buf[pos] = '\0';

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = json_buf;
	response_ctx->body_len = (size_t)pos;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_data_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = api_data_handler,
};

/* -------------------------------------------------------------------------- */
/* GET+POST /api/config                                                        */
/* -------------------------------------------------------------------------- */

/* Buffer for sensor list sub-JSON and full config response. */
static uint8_t cfg_json_buf[2048];

struct sensor_json_ctx {
	int pos;
	int rem;
	bool first;
};

static int location_to_json_cb(const char *name, void *user_data)
{
	struct sensor_json_ctx *ctx = user_data;

	if (!ctx->first) {
		int n = snprintf((char *)cfg_json_buf + ctx->pos, (size_t)(ctx->rem + 1), ",");

		if (n > 0) {
			ctx->pos += MIN(n, ctx->rem);
			ctx->rem -= MIN(n, ctx->rem);
		}
	}
	ctx->first = false;

	int n = snprintf((char *)cfg_json_buf + ctx->pos, (size_t)(ctx->rem + 1), "\"%s\"", name);

	if (n > 0) {
		ctx->pos += MIN(n, ctx->rem);
		ctx->rem -= MIN(n, ctx->rem);
	}
	return 0;
}

static int sensor_to_json_cb(const struct sensor_registry_entry *e, void *user_data)
{
	struct sensor_json_ctx *ctx = user_data;

	if (!ctx->first) {
		int n = snprintf((char *)cfg_json_buf + ctx->pos, (size_t)(ctx->rem + 1), ",");

		if (n > 0) {
			ctx->pos += MIN(n, ctx->rem);
			ctx->rem -= MIN(n, ctx->rem);
		}
	}
	ctx->first = false;

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
	struct sensor_registry_meta smeta;

	sensor_registry_get_meta(e->uid, &smeta);
	int n = snprintf((char *)cfg_json_buf + ctx->pos, (size_t)(ctx->rem + 1),
			 "{\"uid\":%u"
			 ",\"dt_label\":\"%s\""
			 ",\"display_name\":\"%s\",\"location\":\"%s\""
			 ",\"description\":\"%s\",\"enabled\":%s}",
			 e->uid, e->label, smeta.display_name, smeta.location, smeta.description,
			 smeta.enabled ? "true" : "false");
#else
	int n = snprintf((char *)cfg_json_buf + ctx->pos, (size_t)(ctx->rem + 1),
			 "{\"uid\":%u,\"label\":\"%s\"}", e->uid, e->label);
#endif

	if (n > 0) {
		ctx->pos += MIN(n, ctx->rem);
		ctx->rem -= MIN(n, ctx->rem);
	}
	return 0;
}

/* Static JSON buffer for /api/locations. */
static uint8_t loc_json_buf[512];

/* Accumulation buffer for POST body chunks. */
static uint8_t post_buf[1024];
static size_t post_cursor;

static void url_decode(char *s)
{
	char *rd = s, *wr = s;

	while (*rd) {
		if (*rd == '+') {
			*wr++ = ' ';
			rd++;
		} else if (*rd == '%' && rd[1] && rd[2]) {
			char hex[3] = {rd[1], rd[2], '\0'};

			*wr++ = (char)strtol(hex, NULL, 16);
			rd += 3;
		} else {
			*wr++ = *rd++;
		}
	}
	*wr = '\0';
}

/* Extract the value for a given key from a raw form-encoded body string.
 * out must be at least out_len bytes. Returns true if key was found. */
static bool form_extract(const char *body, const char *key, char *out, size_t out_len)
{
	size_t klen = strlen(key);
	const char *p = body;

	while (p && *p) {
		if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
			const char *v = p + klen + 1;
			const char *end = strchr(v, '&');
			size_t vlen = end ? (size_t)(end - v) : strlen(v);

			if (vlen >= out_len) {
				vlen = out_len - 1;
			}
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			url_decode(out);
			return true;
		}
		p = strchr(p, '&');
		if (p) {
			p++;
		}
	}
	return false;
}

static void process_post(const uint8_t *body, size_t len)
{
	/* Buffer must hold the full accumulated POST body (post_buf is 1024 B). */
	static char buf[1025];
	size_t copy_len = MIN(len, sizeof(buf) - 1);

	memcpy(buf, body, copy_len);
	buf[copy_len] = '\0';

	/* Pre-extract loc_name before the loop mutates buf (replaces '&' with '\0'). */
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
				uint32_t ms = (uint32_t)strtoul(val, NULL, 10);
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
				strncpy(sntp_server_buf, val, sizeof(sntp_server_buf) - 1);
				sntp_server_buf[sizeof(sntp_server_buf) - 1] = '\0';
				LOG_INF("sntp server set to %s", sntp_server_buf);
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
					uint32_t uid = (uint32_t)strtoul(tmp, NULL, 10);
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
#endif
			}
		}

		token = amp ? (amp + 1) : NULL;
	}
}

static int api_config_handler(struct http_client_ctx *client, enum http_data_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		post_cursor = 0;
		return 0;
	}

	if (client->method == HTTP_POST) {
		if (request_ctx->data_len > 0) {
			size_t space = sizeof(post_buf) - post_cursor;
			size_t copy = MIN(request_ctx->data_len, space);

			memcpy(post_buf + post_cursor, request_ctx->data, copy);
			post_cursor += copy;
		}

		if (status == HTTP_SERVER_DATA_FINAL) {
			process_post(post_buf, post_cursor);
			post_cursor = 0;

			response_ctx->status = HTTP_303_SEE_OTHER;
			response_ctx->headers = redir_hdrs;
			response_ctx->header_count = ARRAY_SIZE(redir_hdrs);
			response_ctx->body = NULL;
			response_ctx->body_len = 0;
			response_ctx->final_chunk = true;
		}
		return 0;
	}

	/* GET /api/config */
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	int pos = 0;
	int rem = (int)sizeof(cfg_json_buf) - 1;

#define CAPPEND(fmt, ...)                                                                          \
	do {                                                                                       \
		int _cn = snprintf((char *)cfg_json_buf + pos, (size_t)(rem + 1), fmt,             \
				   ##__VA_ARGS__);                                                 \
		if (_cn > 0) {                                                                     \
			pos += MIN(_cn, rem);                                                      \
			rem -= MIN(_cn, rem);                                                      \
		}                                                                                  \
	} while (0)

	CAPPEND("{\"port\":%d,\"trigger_interval_ms\":%u,\"sntp_server\":\"%s\",\"locations\":[",
		CONFIG_HTTP_DASHBOARD_PORT, trigger_interval_ms, sntp_server_buf);

	/* Emit location list using the same ctx pattern. */
	struct sensor_json_ctx lctx = {.pos = pos, .rem = rem, .first = true};

	location_registry_foreach(location_to_json_cb, &lctx);
	pos = lctx.pos;
	rem = lctx.rem;

	CAPPEND("],\"sensors\":[");

	struct sensor_json_ctx sctx = {.pos = pos, .rem = rem, .first = true};

	sensor_registry_foreach(sensor_to_json_cb, &sctx);
	pos = sctx.pos;
	rem = sctx.rem;

	CAPPEND("]}");

#undef CAPPEND

	cfg_json_buf[pos] = '\0';

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = cfg_json_buf;
	response_ctx->body_len = (size_t)pos;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_config_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		},
	.cb = api_config_handler,
};

/* -------------------------------------------------------------------------- */
/* GET /api/locations — JSON list of all named locations                       */
/* -------------------------------------------------------------------------- */

struct loc_list_ctx {
	int pos;
	int rem;
	bool first;
};

static int loc_list_append_cb(const char *name, void *user_data)
{
	struct loc_list_ctx *ctx = user_data;

	if (!ctx->first) {
		int n = snprintf((char *)loc_json_buf + ctx->pos, (size_t)(ctx->rem + 1), ",");

		if (n > 0) {
			ctx->pos += MIN(n, ctx->rem);
			ctx->rem -= MIN(n, ctx->rem);
		}
	}
	ctx->first = false;

	int n = snprintf((char *)loc_json_buf + ctx->pos, (size_t)(ctx->rem + 1), "\"%s\"", name);

	if (n > 0) {
		ctx->pos += MIN(n, ctx->rem);
		ctx->rem -= MIN(n, ctx->rem);
	}
	return 0;
}

static int api_locations_handler(struct http_client_ctx *client, enum http_data_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_DATA_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	int pos = 0;
	int rem = (int)sizeof(loc_json_buf) - 1;
	int n;

	n = snprintf((char *)loc_json_buf + pos, (size_t)(rem + 1), "{\"locations\":[");
	if (n > 0) {
		pos += MIN(n, rem);
		rem -= MIN(n, rem);
	}

	struct loc_list_ctx ctx = {.pos = pos, .rem = rem, .first = true};

	location_registry_foreach(loc_list_append_cb, &ctx);
	pos = ctx.pos;
	rem = ctx.rem;

	n = snprintf((char *)loc_json_buf + pos, (size_t)(rem + 1), "]}");
	if (n > 0) {
		pos += MIN(n, rem);
		rem -= MIN(n, rem);
	}

	loc_json_buf[pos] = '\0';

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = loc_json_buf;
	response_ctx->body_len = (size_t)pos;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_locations_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = api_locations_handler,
};

/* -------------------------------------------------------------------------- */
/* HTTP service + resource registration                                        */
/* -------------------------------------------------------------------------- */

static uint16_t http_port = CONFIG_HTTP_DASHBOARD_PORT;

HTTP_SERVICE_DEFINE(dashboard_svc, NULL, &http_port, 3, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(root_resource, dashboard_svc, "/", &root_detail);
HTTP_RESOURCE_DEFINE(config_resource, dashboard_svc, "/config", &config_page_detail);
HTTP_RESOURCE_DEFINE(api_data_resource, dashboard_svc, "/api/data", &api_data_detail);
HTTP_RESOURCE_DEFINE(api_config_resource, dashboard_svc, "/api/config", &api_config_detail);
HTTP_RESOURCE_DEFINE(api_locations_resource, dashboard_svc, "/api/locations",
		     &api_locations_detail);

/* -------------------------------------------------------------------------- */
/* SYS_INIT                                                                    */
/* -------------------------------------------------------------------------- */

static int http_dashboard_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &http_dashboard_listener, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("Failed to add sensor_event observer: %d", rc);
		return rc;
	}

	rc = http_server_start();
	if (rc != 0) {
		LOG_ERR("Failed to start HTTP server: %d", rc);
		return rc;
	}

	LOG_INF("HTTP dashboard started on port %d", CONFIG_HTTP_DASHBOARD_PORT);
	return 0;
}

SYS_INIT(http_dashboard_init, APPLICATION, 97);
