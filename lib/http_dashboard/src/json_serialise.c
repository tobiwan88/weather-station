/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <location_registry/location_registry.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>

#include "json_serialise.h"
#include "sensor_history.h"

/* Append to a (buf, pos, rem) triple. Requires local variables named exactly
 * buf (uint8_t *), pos (int), rem (int). */
#define JAPPEND(fmt, ...)                                                                          \
	do {                                                                                       \
		int _n = snprintf((char *)buf + pos, (size_t)(rem + 1), fmt, ##__VA_ARGS__);       \
		if (_n > 0) {                                                                      \
			pos += MIN(_n, rem);                                                       \
			rem -= MIN(_n, rem);                                                       \
		}                                                                                  \
	} while (0)

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

	buf[pos] = '\0';
	return (size_t)pos;
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
	JAPPEND("\"%s\"", name);

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

	if (!ctx->first) {
		JAPPEND(",");
	}
	ctx->first = false;

#ifdef CONFIG_SENSOR_REGISTRY_USER_META
	struct sensor_registry_meta smeta;

	sensor_registry_get_meta(e->uid, &smeta);
	JAPPEND("{\"uid\":%u"
		",\"dt_label\":\"%s\""
		",\"display_name\":\"%s\",\"location\":\"%s\""
		",\"description\":\"%s\",\"enabled\":%s}",
		e->uid, e->label, smeta.display_name, smeta.location, smeta.description,
		smeta.enabled ? "true" : "false");
#else
	JAPPEND("{\"uid\":%u,\"label\":\"%s\"}", e->uid, e->label);
#endif

	ctx->pos = pos;
	ctx->rem = rem;
	return 0;
}

size_t config_to_json(uint16_t port, uint32_t trigger_ms, const char *sntp_server, uint8_t *buf,
		      size_t buf_size)
{
	int pos = 0;
	int rem = (int)buf_size - 1;

	JAPPEND("{\"port\":%d,\"trigger_interval_ms\":%u,\"sntp_server\":\"%s\",\"locations\":[",
		port, trigger_ms, sntp_server);

	struct json_write_ctx lctx = {.buf = buf, .pos = pos, .rem = rem, .first = true};

	location_registry_foreach(location_name_to_json_cb, &lctx);
	pos = lctx.pos;
	rem = lctx.rem;

	JAPPEND("],\"sensors\":[");

	struct json_write_ctx sctx = {.buf = buf, .pos = pos, .rem = rem, .first = true};

	sensor_registry_foreach(sensor_entry_to_json_cb, &sctx);
	pos = sctx.pos;
	rem = sctx.rem;

	JAPPEND("]}");

	buf[pos] = '\0';
	return (size_t)pos;
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
	return (size_t)pos;
}
