/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file http_dashboard.c
 * @brief HTTP weather dashboard: live Chart.js graph + configuration page.
 *
 * Architecture:
 *   - ZBUS_LISTENER_DEFINE subscribes to sensor_event_chan (priority 97).
 *   - Per-sensor ring buffers store the last HISTORY_SIZE samples (sensor_history.c).
 *   - k_spinlock protects ring buffers (callback may run from ISR/timer context).
 *   - HTTP_SERVICE_DEFINE + five HTTP_RESOURCE_DEFINE handle the five URLs.
 *   - SYS_INIT at APPLICATION 97 registers the zbus observer and starts the
 *     HTTP server.
 *
 * Auxiliary modules (all in src/):
 *   - sensor_history.c  — ring-buffer data model and snapshot helpers
 *   - form_parse.c      — URL decode and form field extraction
 *   - json_serialise.c  — JSON builders for /api/data, /api/config, /api/locations
 *   - process_post.c    — POST dispatch, config state, zbus publications
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

#include "json_serialise.h"
#include "process_post.h"
#include "sensor_history.h"

LOG_MODULE_REGISTER(http_dashboard, LOG_LEVEL_INF);

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

/* POST /api/config success response body */
static const char post_ok[] = "{\"ok\":true}";

/* -------------------------------------------------------------------------- */
/* Static output buffers                                                       */
/* -------------------------------------------------------------------------- */

/* Protected by resource holder (one HTTP request at a time). */
static uint8_t json_buf[8192];
static uint8_t cfg_json_buf[2048];
static uint8_t loc_json_buf[512];

/* Accumulation buffer for POST body chunks. */
static uint8_t post_buf[1024];
static size_t post_cursor;

/* -------------------------------------------------------------------------- */
/* zbus listener                                                               */
/* -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	history_record_event(zbus_chan_const_msg(chan));
}

ZBUS_LISTENER_DEFINE(http_dashboard_listener, sensor_event_cb);

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

	history_do_snapshot();

	size_t len = history_to_json(history_get_snap(), CONFIG_HTTP_DASHBOARD_MAX_SENSORS,
				     json_buf, sizeof(json_buf));

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = json_buf;
	response_ctx->body_len = len;
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

			response_ctx->status = HTTP_200_OK;
			response_ctx->headers = json_ct_hdr;
			response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
			response_ctx->body = (const uint8_t *)post_ok;
			response_ctx->body_len = sizeof(post_ok) - 1;
			response_ctx->final_chunk = true;
		}
		return 0;
	}

	/* GET /api/config */
	if (status != HTTP_SERVER_DATA_FINAL) {
		return 0;
	}

	char sntp_snap[64];

	config_state_copy_sntp_server(sntp_snap, sizeof(sntp_snap));

	size_t len = config_to_json(CONFIG_HTTP_DASHBOARD_PORT, config_state_get_trigger_ms(),
				    sntp_snap, cfg_json_buf, sizeof(cfg_json_buf));

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = cfg_json_buf;
	response_ctx->body_len = len;
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

	size_t len = locations_to_json(loc_json_buf, sizeof(loc_json_buf));

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = loc_json_buf;
	response_ctx->body_len = len;
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
