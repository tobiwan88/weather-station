/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <location_registry/location_registry.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>

#if defined(CONFIG_MQTT_PUBLISHER)
#	include <mqtt_publisher/mqtt_publisher.h>
#endif

#include "json_serialise.h"
#include "sensor_history.h"

/* Append to a (buf, pos, rem) triple. Requires local variables named exactly
 * buf (uint8_t *), pos (int), rem (int).
 * Sets rem = -1 on overflow; all subsequent calls are no-ops. */
#define JAPPEND(fmt, ...)                                                                          \
	do {                                                                                       \
		if (rem > 0) {                                                                     \
			int _n = snprintf((char *)buf + pos, (size_t)(rem + 1), fmt,               \
					  ##__VA_ARGS__);                                          \
			if (_n > rem) {                                                            \
				rem = -1;                                                          \
			} else if (_n > 0) {                                                       \
				pos += _n;                                                         \
				rem -= _n;                                                         \
			}                                                                          \
		}                                                                                  \
	} while (0)

/* Append a JSON-escaped string (no surrounding quotes). Escapes ", \, and
 * control characters (<0x20) as \uXXXX. Requires local buf/pos/rem.
 * @p s must not be NULL. Sets *rem = -1 on overflow; caller detects overflow
 * by checking rem < 0. */
static void json_append_str(uint8_t *buf, int *pos, int *rem, const char *s)
{
	__ASSERT_NO_MSG(s != NULL);
	if (*rem <= 0) {
		*rem = -1;
		return;
	}
	for (; *s != '\0' && *rem > 0; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') {
			if (*rem >= 2) {
				buf[(*pos)++] = '\\';
				buf[(*pos)++] = c;
				*rem -= 2;
			} else {
				*rem = -1;
			}
		} else if (c < 0x20) {
			if (*rem >= 6) {
				int n = snprintf((char *)buf + *pos, (size_t)(*rem + 1), "\\u%04x",
						 c);
				if (n > 0) {
					if (n > *rem) {
						*rem = -1;
					} else {
						*pos += n;
						*rem -= n;
					}
				}
			} else {
				*rem = -1;
			}
		} else {
			buf[(*pos)++] = c;
			(*rem)--;
		}
	}
}

#define JAPPEND_STR(s) json_append_str(buf, &pos, &rem, (s))

/* -------------------------------------------------------------------------- */
/* sensor_type_str                                                             */
/* -------------------------------------------------------------------------- */

const char *sensor_type_str(enum sensor_type t)
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

/* -------------------------------------------------------------------------- */
/* history_to_json — /api/data                                                */
/* -------------------------------------------------------------------------- */

size_t history_to_json(const struct sensor_history *snap, int n_sensors, uint8_t *buf,
		       size_t buf_size)
{
	int pos = 0;
	int rem = (int)buf_size - 1;

	JAPPEND("{\"sensors\":[");

	bool first_sensor = true;

	for (int i = 0; i < n_sensors; i++) {
		if (!snap[i].valid || snap[i].count == 0) {
			continue;
		}

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
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
		JAPPEND("{\"uid\":%u,\"label\":\"", snap[i].uid);
		JAPPEND_STR(label);
		JAPPEND("\",\"location\":\"");
		JAPPEND_STR(location);
		JAPPEND("\",\"description\":\"");
		JAPPEND_STR(description);
		JAPPEND("\",\"type\":\"%s\",\"readings\":[", sensor_type_str(snap[i].type));
#else
		JAPPEND("{\"uid\":%u,\"label\":\"", snap[i].uid);
		JAPPEND_STR(label);
		JAPPEND("\",\"location\":\"");
		JAPPEND_STR(location);
		JAPPEND("\",\"type\":\"%s\",\"readings\":[", sensor_type_str(snap[i].type));
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

	buf[pos] = '\0';
	return (rem < 0) ? 0 : (size_t)pos;
}

/* -------------------------------------------------------------------------- */
/* config_to_json — GET /api/config                                           */
/* -------------------------------------------------------------------------- */

struct json_write_ctx {
	uint8_t *buf;
	int pos;
	int rem;
	bool first;
};

static int location_name_to_json_cb(const char *name, void *user_data)
{
	struct json_write_ctx *ctx = user_data;
	uint8_t *buf = ctx->buf;
	int pos = ctx->pos;
	int rem = ctx->rem;

	if (!ctx->first) {
		JAPPEND(",");
	}
	ctx->first = false;
	JAPPEND("\"");
	JAPPEND_STR(name);
	JAPPEND("\"");

	ctx->pos = pos;
	ctx->rem = rem;
	return 0;
}

static int sensor_entry_to_json_cb(const struct sensor_registry_entry *e, void *user_data)
{
	struct json_write_ctx *ctx = user_data;
	uint8_t *buf = ctx->buf;
	int pos = ctx->pos;
	int rem = ctx->rem;

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
	struct sensor_registry_meta smeta;

	/* Validate before writing the comma so a missing metadata entry
	 * does not leave a stray separator in the JSON output. */
	if (sensor_registry_get_meta(e->uid, &smeta) != 0) {
		return 0;
	}
	if (!ctx->first) {
		JAPPEND(",");
	}
	ctx->first = false;
	JAPPEND("{\"uid\":%u,\"dt_label\":\"", e->uid);
	JAPPEND_STR(e->label);
	JAPPEND("\",\"display_name\":\"");
	JAPPEND_STR(smeta.display_name);
	JAPPEND("\",\"location\":\"");
	JAPPEND_STR(smeta.location);
	JAPPEND("\",\"description\":\"");
	JAPPEND_STR(smeta.description);
	JAPPEND("\",\"enabled\":%s}", smeta.enabled ? "true" : "false");
#else
	if (!ctx->first) {
		JAPPEND(",");
	}
	ctx->first = false;
	JAPPEND("{\"uid\":%u,\"label\":\"", e->uid);
	JAPPEND_STR(e->label);
	JAPPEND("\"}");
#endif

	ctx->pos = pos;
	ctx->rem = rem;
	return 0;
}

size_t config_to_json(uint16_t port, uint32_t trigger_ms, const char *sntp_server,
		      const void *mqtt_cfg, uint8_t *buf, size_t buf_size)
{
	int pos = 0;
	int rem = (int)buf_size - 1;

	JAPPEND("{\"port\":%d,\"trigger_interval_ms\":%u,\"sntp_server\":\"", port, trigger_ms);
	JAPPEND_STR(sntp_server);
	JAPPEND("\",\"locations\":[");

	struct json_write_ctx lctx = {.buf = buf, .pos = pos, .rem = rem, .first = true};

	location_registry_foreach(location_name_to_json_cb, &lctx);
	pos = lctx.pos;
	rem = lctx.rem;

	JAPPEND("],\"sensors\":[");

	struct json_write_ctx sctx = {.buf = buf, .pos = pos, .rem = rem, .first = true};

	sensor_registry_foreach(sensor_entry_to_json_cb, &sctx);
	pos = sctx.pos;
	rem = sctx.rem;

	JAPPEND("]");

	if (mqtt_cfg) {
		const struct mqtt_publisher_config *mc = mqtt_cfg;

		JAPPEND(",\"mqtt\":{\"enabled\":%s,\"host\":\"", mc->enabled ? "true" : "false");
		JAPPEND_STR(mc->host);
		JAPPEND("\",\"port\":%u,\"user\":\"", mc->port);
		JAPPEND_STR(mc->username);
		JAPPEND("\",\"gateway\":\"");
		JAPPEND_STR(mc->gateway_name);
		JAPPEND("\",\"keepalive\":%u}", mc->keepalive);
	}

	JAPPEND("}");

	buf[pos] = '\0';
	return (rem < 0) ? 0 : (size_t)pos;
}

/* -------------------------------------------------------------------------- */
/* locations_to_json — GET /api/locations                                     */
/* -------------------------------------------------------------------------- */

size_t locations_to_json(uint8_t *buf, size_t buf_size)
{
	int pos = 0;
	int rem = (int)buf_size - 1;

	JAPPEND("{\"locations\":[");

	struct json_write_ctx ctx = {.buf = buf, .pos = pos, .rem = rem, .first = true};

	location_registry_foreach(location_name_to_json_cb, &ctx);
	pos = ctx.pos;
	rem = ctx.rem;

	JAPPEND("]}");

	buf[pos] = '\0';
	return (rem < 0) ? 0 : (size_t)pos;
}
