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

#if defined(CONFIG_MQTT_PUBLISHER)
#	include <mqtt_publisher/mqtt_publisher.h>
#endif

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
#	include "auth.h"
#endif
#include "json_serialise.h"
#include "process_post.h"
#include "sensor_history.h"

LOG_MODULE_REGISTER(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
/* Capture the Authorization header so handlers can validate bearer tokens. */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(auth_hdr_capture, "Authorization");
#endif

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

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
static const char auth_body[] = "{\"error\":\"unauthorized\"}";
static const struct http_header auth_hdrs[] = {
	{.name = "Content-Type", .value = "application/json"},
	{.name = "WWW-Authenticate", .value = "Bearer realm=\"weather-station\""},
};

static void respond_401(struct http_response_ctx *rsp)
{
	LOG_INF("respond_401: ENTRY rsp=%p final_chunk=%d", (void *)rsp, rsp->final_chunk);
	LOG_DBG("respond_401: sending 401 Unauthorized");
	rsp->status = HTTP_401_UNAUTHORIZED;
	rsp->headers = auth_hdrs;
	rsp->header_count = ARRAY_SIZE(auth_hdrs);
	rsp->body = (const uint8_t *)auth_body;
	rsp->body_len = sizeof(auth_body) - 1;
	rsp->final_chunk = true;
	LOG_INF("respond_401: EXIT rsp->status=%d body_len=%zu final_chunk=%d", rsp->status,
		rsp->body_len, rsp->final_chunk);
}
#endif /* CONFIG_HTTP_DASHBOARD_AUTH */

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

static int root_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
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

static int config_page_handler(struct http_client_ctx *client, enum http_transaction_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
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

static int api_data_handler(struct http_client_ctx *client, enum http_transaction_status status,
			    const struct http_request_ctx *request_ctx,
			    struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	LOG_DBG("GET /api/data: serving");

#if defined(CONFIG_HTTP_DASHBOARD_AUTH) && defined(CONFIG_HTTP_DASHBOARD_AUTH_PROTECT_READ)
	if (!auth_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}
#endif

	history_do_snapshot();

	size_t len = history_to_json(history_get_snap(), CONFIG_HTTP_DASHBOARD_MAX_SENSORS,
				     json_buf, sizeof(json_buf));

	if (len == 0) {
		LOG_ERR("history_to_json: buffer overflow");
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}
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

static int api_config_handler(struct http_client_ctx *client, enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("api_config_handler: ENTRY client=%p method=%d status=%d data_len=%zu",
		(void *)client, client->method, status, request_ctx->data_len);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		LOG_INF("api_config_handler: ABORTED client=%p", (void *)client);
		post_cursor = 0;
		return 0;
	}

	if (client->method == HTTP_POST) {
		LOG_DBG("api_config_handler: POST status=%d, data_len=%zu, post_cursor=%zu", status,
			request_ctx->data_len, post_cursor);
		if (request_ctx->data_len > 0) {
			size_t space = sizeof(post_buf) - post_cursor;
			size_t copy = MIN(request_ctx->data_len, space);

			memcpy(post_buf + post_cursor, request_ctx->data, copy);
			post_cursor += copy;
			LOG_DBG("POST /api/config: chunk %zuB, cursor=%zu", copy, post_cursor);
		}

		if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
			LOG_DBG("api_config_handler: HTTP_SERVER_REQUEST_DATA_FINAL for POST");
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
			LOG_DBG("api_config_handler: calling auth_check");
			if (!auth_check(request_ctx)) {
				post_cursor = 0;
				LOG_DBG("api_config_handler: auth denied, calling respond_401");
				respond_401(response_ctx);
				LOG_DBG("POST /api/config: auth denied, responded 401");
				return 0;
			}
			LOG_DBG("api_config_handler: auth passed");
#endif
			LOG_DBG("api_config_handler: calling process_post with %zu bytes",
				post_cursor);
			process_post(post_buf, post_cursor);
			LOG_DBG("api_config_handler: process_post returned");
			post_cursor = 0;

			LOG_DBG("api_config_handler: setting up 200 OK response");
			response_ctx->status = HTTP_200_OK;
			response_ctx->headers = json_ct_hdr;
			response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
			response_ctx->body = (const uint8_t *)post_ok;
			response_ctx->body_len = sizeof(post_ok) - 1;
			response_ctx->final_chunk = true;
			LOG_INF("api_config_handler: POST completed, final_chunk=true, "
				"body_len=%zu",
				response_ctx->body_len);
			LOG_DBG("POST /api/config: responded 200 OK");
		} else if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
			LOG_INF("api_config_handler: TRANSACTION_COMPLETE client=%p",
				(void *)client);
			post_cursor = 0;
		}
		return 0;
	}

	/* GET /api/config */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

#if defined(CONFIG_HTTP_DASHBOARD_AUTH) && defined(CONFIG_HTTP_DASHBOARD_AUTH_PROTECT_READ)
	if (!auth_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}
#endif

	char sntp_snap[64];

	config_state_copy_sntp_server(sntp_snap, sizeof(sntp_snap));

#if defined(CONFIG_MQTT_PUBLISHER)
	struct mqtt_publisher_config mqtt_cfg;

	mqtt_publisher_get_config(&mqtt_cfg);
	size_t len = config_to_json(CONFIG_HTTP_DASHBOARD_PORT, config_state_get_trigger_ms(),
				    sntp_snap, &mqtt_cfg, cfg_json_buf, sizeof(cfg_json_buf));
#else
	size_t len = config_to_json(CONFIG_HTTP_DASHBOARD_PORT, config_state_get_trigger_ms(),
				    sntp_snap, NULL, cfg_json_buf, sizeof(cfg_json_buf));
#endif

	if (len == 0) {
		LOG_ERR("config_to_json: buffer overflow");
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}
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

static int api_locations_handler(struct http_client_ctx *client,
				 enum http_transaction_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	LOG_DBG("GET /api/locations: serving");

#if defined(CONFIG_HTTP_DASHBOARD_AUTH) && defined(CONFIG_HTTP_DASHBOARD_AUTH_PROTECT_READ)
	if (!auth_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}
#endif

	size_t len = locations_to_json(loc_json_buf, sizeof(loc_json_buf));

	if (len == 0) {
		LOG_ERR("locations_to_json: buffer overflow");
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}
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
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
	int auth_rc = auth_init();

	if (auth_rc != 0) {
		LOG_ERR("auth_init failed: %d", auth_rc);
		return auth_rc;
	}
#endif

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

#if defined(CONFIG_HTTP_DASHBOARD_AUTH) && defined(CONFIG_HTTP_DASHBOARD_AUTH_SHELL)
#	include <zephyr/shell/shell.h>

static int cmd_token_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	char tok[AUTH_TOKEN_STR_LEN + 1];

	auth_token_copy(tok, sizeof(tok));
	shell_print(sh, "Authorization: Bearer %s", tok);
	return 0;
}

static int cmd_token_rotate(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = auth_token_rotate();

	if (rc != 0) {
		shell_error(sh, "token rotation failed: %d", rc);
		return rc;
	}

	char tok[AUTH_TOKEN_STR_LEN + 1];

	auth_token_copy(tok, sizeof(tok));
	shell_print(sh, "New token — Authorization: Bearer %s", tok);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_token,
			       SHELL_CMD(show, NULL, "Print current bearer token", cmd_token_show),
			       SHELL_CMD(rotate, NULL, "Generate and apply a new random token",
					 cmd_token_rotate),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_http_dashboard,
			       SHELL_CMD(token, &sub_token, "Token management", NULL),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(http_dashboard, &sub_http_dashboard, "HTTP dashboard commands", NULL);

#endif /* CONFIG_HTTP_DASHBOARD_AUTH && CONFIG_HTTP_DASHBOARD_AUTH_SHELL */

SYS_INIT(http_dashboard_init, APPLICATION, 97);
