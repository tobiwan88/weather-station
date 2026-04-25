/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file http_dashboard.c
 * @brief HTTP weather dashboard: live Chart.js graph + configuration page.
 *
 * Architecture:
 *   - ZBUS_LISTENER_DEFINE subscribes to sensor_event_chan (priority 97).
 *   - Per-sensor ring buffers store the last HISTORY_SIZE samples (sensor_history.c).
 *   - k_spinlock protects ring buffers (callback may run from ISR/timer context).
 *   - HTTP_SERVICE_DEFINE + HTTP_RESOURCE_DEFINE handle the URLs.
 *   - SYS_INIT at APPLICATION 97 registers the zbus observer and starts the
 *     HTTP server.
 *
 * Auth:
 *   - Browser users: POST /api/login → session cookie (HttpOnly).
 *   - Automation: Authorization: Bearer <api_token> header.
 *   - Page GETs (/, /config): session cookie only → redirect to /login on fail.
 *   - API endpoints: session cookie OR bearer token → 401 on fail.
 *   - Public routes: GET /login, POST /api/login.
 *
 * Auxiliary modules (all in src/):
 *   - sensor_history.c  — ring-buffer data model and snapshot helpers
 *   - form_parse.c      — URL decode and form field extraction
 *   - json_serialise.c  — JSON builders for /api/data, /api/config, /api/locations
 *   - process_post.c    — POST dispatch, config state, zbus publications
 *   - auth.c            — session table, credentials, API token, settings persistence
 */

#include <stdint.h>
#include <stdio.h>
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
#include "form_parse.h"
#include "json_serialise.h"
#include "process_post.h"
#include "sensor_history.h"

LOG_MODULE_REGISTER(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
/* Capture Cookie (session) and Authorization (API bearer token) headers. */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(cookie_hdr_capture, "Cookie");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(auth_hdr_capture, "Authorization");
#endif

/* -------------------------------------------------------------------------- */
/* HTML page content                                                            */
/* -------------------------------------------------------------------------- */

static const char dashboard_html[] = {
#include "dashboard.html.inc"
	'\0'};

static const char config_html[] = {
#include "config.html.inc"
	'\0'};

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
static const char login_html[] = {
#	include "login.html.inc"
	'\0'};
#endif

/* -------------------------------------------------------------------------- */
/* Common response helpers                                                      */
/* -------------------------------------------------------------------------- */

static const struct http_header html_ct_hdr[] = {
	{.name = "Content-Type", .value = "text/html; charset=utf-8"},
};

static const struct http_header json_ct_hdr[] = {
	{.name = "Content-Type", .value = "application/json"},
};

static const char post_ok[] = "{\"ok\":true}";

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)

static const char unauth_body[] = "{\"error\":\"unauthorized\"}";
static const struct http_header unauth_hdrs[] = {
	{.name = "Content-Type", .value = "application/json"},
};

static void respond_401(struct http_response_ctx *rsp)
{
	rsp->status = HTTP_401_UNAUTHORIZED;
	rsp->headers = unauth_hdrs;
	rsp->header_count = ARRAY_SIZE(unauth_hdrs);
	rsp->body = (const uint8_t *)unauth_body;
	rsp->body_len = sizeof(unauth_body) - 1;
	rsp->final_chunk = true;
}

static void respond_redirect_login(struct http_response_ctx *rsp)
{
	static const struct http_header loc_hdr[] = {
		{.name = "location", .value = "/login"},
	};
	rsp->status = HTTP_302_FOUND;
	rsp->headers = loc_hdr;
	rsp->header_count = ARRAY_SIZE(loc_hdr);
	rsp->final_chunk = true;
}

/* respond_200_with_cookie — send 200 JSON and set an HttpOnly session cookie.
 * @p sc_buf must be a caller-provided buffer >= 64 bytes that outlives the
 * HTTP response transmission (i.e. static or per-handler static).
 */
static void respond_200_with_cookie(struct http_response_ctx *rsp, const char *token,
				    const char *json_body, size_t body_len, char *sc_buf,
				    size_t sc_buf_len)
{
	snprintf(sc_buf, sc_buf_len, "session=%s; HttpOnly; Path=/; SameSite=Strict", token);

	/* Two-element header array: Content-Type + Set-Cookie. */
	static struct http_header resp_hdrs[2] = {
		{.name = "Content-Type", .value = "application/json"},
		{.name = "set-cookie", .value = NULL}, /* value filled below */
	};
	resp_hdrs[1].value = sc_buf;

	rsp->status = HTTP_200_OK;
	rsp->headers = resp_hdrs;
	rsp->header_count = ARRAY_SIZE(resp_hdrs);
	rsp->body = (const uint8_t *)json_body;
	rsp->body_len = body_len;
	rsp->final_chunk = true;
}

#endif /* CONFIG_HTTP_DASHBOARD_AUTH */

/* -------------------------------------------------------------------------- */
/* Static output buffers                                                        */
/* -------------------------------------------------------------------------- */

static uint8_t json_buf[8192];
static uint8_t cfg_json_buf[2048];
static uint8_t loc_json_buf[512];

/* POST body accumulation — /api/config */
static uint8_t post_buf[1024];
static size_t post_cursor;

/* POST body accumulation — /api/login (separate buffer, no auth required) */
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
static uint8_t login_buf[256];
static size_t login_cursor;

/* POST body accumulation — /api/change-credentials */
static uint8_t creds_buf[512];
static size_t creds_cursor;

/* POST body accumulation — /api/token/rotate (no body needed but handler
 * must consume any chunks gracefully) */
static uint8_t token_rotate_buf[16];
static size_t token_rotate_cursor;
#endif

/* -------------------------------------------------------------------------- */
/* zbus listener                                                                */
/* -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	history_record_event(zbus_chan_const_msg(chan));
}

ZBUS_LISTENER_DEFINE(http_dashboard_listener, sensor_event_cb);

/* -------------------------------------------------------------------------- */
/* GET /login — login page (public)                                            */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
static int login_page_handler(struct http_client_ctx *client, enum http_transaction_status status,
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
		response_ctx->body = (const uint8_t *)login_html;
		response_ctx->body_len = sizeof(login_html) - 1;
		response_ctx->final_chunk = true;
	}
	return 0;
}

static struct http_resource_detail_dynamic login_page_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = login_page_handler,
};
#endif /* CONFIG_HTTP_DASHBOARD_AUTH */

/* -------------------------------------------------------------------------- */
/* GET / — dashboard page                                                       */
/* -------------------------------------------------------------------------- */

static int root_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
		if (!auth_session_check(request_ctx)) {
			respond_redirect_login(response_ctx);
			return 0;
		}
#endif
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
/* GET /config — configuration page                                            */
/* -------------------------------------------------------------------------- */

static int config_page_handler(struct http_client_ctx *client, enum http_transaction_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
		if (!auth_session_check(request_ctx)) {
			respond_redirect_login(response_ctx);
			return 0;
		}
#endif
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
/* POST /api/login — authenticate and create a session (public)                */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
static int api_login_handler(struct http_client_ctx *client, enum http_transaction_status status,
			     const struct http_request_ctx *request_ctx,
			     struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		login_cursor = 0;
		return 0;
	}

	if (request_ctx->data_len > 0) {
		size_t space = sizeof(login_buf) - login_cursor;
		size_t copy = MIN(request_ctx->data_len, space);

		memcpy(login_buf + login_cursor, request_ctx->data, copy);
		login_cursor += copy;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	/* NUL-terminate and parse username/password from form body. */
	if (login_cursor < sizeof(login_buf)) {
		login_buf[login_cursor] = '\0';
	} else {
		login_buf[sizeof(login_buf) - 1] = '\0';
	}
	char username[AUTH_CRED_MAX];
	char password[AUTH_CRED_MAX];

	username[0] = '\0';
	password[0] = '\0';
	form_extract((const char *)login_buf, "username", username, sizeof(username));
	form_extract((const char *)login_buf, "password", password, sizeof(password));
	login_cursor = 0;

	/* Static Set-Cookie buffer — safe because the login handler is called
	 * once per request and the response is transmitted before the next call. */
	static char sc_buf[64];
	char token[AUTH_SESSION_TOKEN_LEN + 1];
	int rc = auth_login(username, password, token, sizeof(token));

	if (rc == -EACCES) {
		LOG_DBG("POST /api/login: bad credentials");
		static const char bad_creds[] = "{\"error\":\"bad credentials\"}";

		response_ctx->status = HTTP_401_UNAUTHORIZED;
		response_ctx->headers = unauth_hdrs;
		response_ctx->header_count = ARRAY_SIZE(unauth_hdrs);
		response_ctx->body = (const uint8_t *)bad_creds;
		response_ctx->body_len = sizeof(bad_creds) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}
	if (rc != 0) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	LOG_INF("POST /api/login: session created");
	respond_200_with_cookie(response_ctx, token, post_ok, sizeof(post_ok) - 1, sc_buf,
				sizeof(sc_buf));
	return 0;
}

static struct http_resource_detail_dynamic api_login_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = api_login_handler,
};

/* -------------------------------------------------------------------------- */
/* POST /api/logout — invalidate session                                        */
/* -------------------------------------------------------------------------- */

static int api_logout_handler(struct http_client_ctx *client, enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	auth_logout(request_ctx);
	LOG_INF("POST /api/logout: session invalidated");

	/* Clear the browser cookie. */
	static const struct http_header logout_hdrs[] = {
		{.name = "Content-Type", .value = "application/json"},
		{.name = "set-cookie", .value = "session=; HttpOnly; Path=/; Max-Age=0"},
	};
	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = logout_hdrs;
	response_ctx->header_count = ARRAY_SIZE(logout_hdrs);
	response_ctx->body = (const uint8_t *)post_ok;
	response_ctx->body_len = sizeof(post_ok) - 1;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_logout_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = api_logout_handler,
};

/* -------------------------------------------------------------------------- */
/* POST /api/change-credentials                                                 */
/* -------------------------------------------------------------------------- */

static int api_change_creds_handler(struct http_client_ctx *client,
				    enum http_transaction_status status,
				    const struct http_request_ctx *request_ctx,
				    struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		creds_cursor = 0;
		return 0;
	}

	if (request_ctx->data_len > 0) {
		size_t space = sizeof(creds_buf) - creds_cursor;
		size_t copy = MIN(request_ctx->data_len, space);

		memcpy(creds_buf + creds_cursor, request_ctx->data, copy);
		creds_cursor += copy;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	if (!auth_api_check(request_ctx)) {
		creds_cursor = 0;
		respond_401(response_ctx);
		return 0;
	}

	/* NUL-terminate the buffer before passing to form_extract. */
	if (creds_cursor < sizeof(creds_buf)) {
		creds_buf[creds_cursor] = '\0';
	} else {
		creds_buf[sizeof(creds_buf) - 1] = '\0';
	}

	char old_user[AUTH_CRED_MAX];
	char old_pass[AUTH_CRED_MAX];
	char new_user[AUTH_CRED_MAX];
	char new_pass[AUTH_CRED_MAX];

	old_user[0] = old_pass[0] = new_user[0] = new_pass[0] = '\0';
	form_extract((const char *)creds_buf, "old_user", old_user, sizeof(old_user));
	form_extract((const char *)creds_buf, "old_pass", old_pass, sizeof(old_pass));
	form_extract((const char *)creds_buf, "new_user", new_user, sizeof(new_user));
	form_extract((const char *)creds_buf, "new_pass", new_pass, sizeof(new_pass));
	creds_cursor = 0;

	int rc = auth_change_credentials(old_user, old_pass, new_user, new_pass);

	if (rc == -EACCES) {
		static const char wrong_creds[] = "{\"error\":\"wrong credentials\"}";

		response_ctx->status = HTTP_403_FORBIDDEN;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = (const uint8_t *)wrong_creds;
		response_ctx->body_len = sizeof(wrong_creds) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}
	if (rc == -EINVAL) {
		static const char bad_input[] = "{\"error\":\"invalid input\"}";

		response_ctx->status = HTTP_400_BAD_REQUEST;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = (const uint8_t *)bad_input;
		response_ctx->body_len = sizeof(bad_input) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}
	if (rc != 0) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = (const uint8_t *)post_ok;
	response_ctx->body_len = sizeof(post_ok) - 1;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_change_creds_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = api_change_creds_handler,
};

/* -------------------------------------------------------------------------- */
/* POST /api/token/rotate                                                       */
/* -------------------------------------------------------------------------- */

static int api_token_rotate_handler(struct http_client_ctx *client,
				    enum http_transaction_status status,
				    const struct http_request_ctx *request_ctx,
				    struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		token_rotate_cursor = 0;
		return 0;
	}

	if (request_ctx->data_len > 0) {
		size_t space = sizeof(token_rotate_buf) - token_rotate_cursor;
		size_t copy = MIN(request_ctx->data_len, space);

		memcpy(token_rotate_buf + token_rotate_cursor, request_ctx->data, copy);
		token_rotate_cursor += copy;
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	token_rotate_cursor = 0;

	if (!auth_api_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}

	int rc = auth_token_rotate();

	if (rc != 0) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	/* Return new token in JSON so config page can display it. */
	static uint8_t tok_json_buf[64];
	char tok[AUTH_SESSION_TOKEN_LEN + 1];

	auth_token_copy(tok, sizeof(tok));
	snprintf((char *)tok_json_buf, sizeof(tok_json_buf), "{\"token\":\"%s\"}", tok);

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = tok_json_buf;
	response_ctx->body_len = strlen((char *)tok_json_buf);
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic api_token_rotate_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = api_token_rotate_handler,
};
#endif /* CONFIG_HTTP_DASHBOARD_AUTH */

/* -------------------------------------------------------------------------- */
/* GET /api/data — JSON sensor history                                          */
/* -------------------------------------------------------------------------- */

static int api_data_handler(struct http_client_ctx *client, enum http_transaction_status status,
			    const struct http_request_ctx *request_ctx,
			    struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
	if (!auth_api_check(request_ctx)) {
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
/* GET+POST /api/config                                                         */
/* -------------------------------------------------------------------------- */

static int api_config_handler(struct http_client_ctx *client, enum http_transaction_status status,
			      const struct http_request_ctx *request_ctx,
			      struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("api_config_handler: ENTRY client=%p method=%d status=%d data_len=%zu",
		(void *)client, client->method, status, request_ctx->data_len);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
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

		if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
			if (!auth_api_check(request_ctx)) {
				post_cursor = 0;
				respond_401(response_ctx);
				return 0;
			}
#endif
			process_post(post_buf, post_cursor);
			post_cursor = 0;

			response_ctx->status = HTTP_200_OK;
			response_ctx->headers = json_ct_hdr;
			response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
			response_ctx->body = (const uint8_t *)post_ok;
			response_ctx->body_len = sizeof(post_ok) - 1;
			response_ctx->final_chunk = true;
			LOG_INF("POST /api/config: 200 OK");
		} else if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
			post_cursor = 0;
		}
		return 0;
	}

	/* GET /api/config */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
	if (!auth_api_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}
#endif

	char sntp_snap[64];

	config_state_copy_sntp_server(sntp_snap, sizeof(sntp_snap));

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
	char api_token[AUTH_SESSION_TOKEN_LEN + 1];

	auth_token_copy(api_token, sizeof(api_token));
#else
	const char *api_token = NULL;
#endif

#if defined(CONFIG_MQTT_PUBLISHER)
	struct mqtt_publisher_config mqtt_cfg;

	mqtt_publisher_get_config(&mqtt_cfg);
	size_t len =
		config_to_json(CONFIG_HTTP_DASHBOARD_PORT, config_state_get_trigger_ms(), sntp_snap,
			       api_token, &mqtt_cfg, cfg_json_buf, sizeof(cfg_json_buf));
#else
	size_t len = config_to_json(CONFIG_HTTP_DASHBOARD_PORT, config_state_get_trigger_ms(),
				    sntp_snap, api_token, NULL, cfg_json_buf, sizeof(cfg_json_buf));
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
/* GET /api/locations — JSON list of all named locations                        */
/* -------------------------------------------------------------------------- */

static int api_locations_handler(struct http_client_ctx *client,
				 enum http_transaction_status status,
				 const struct http_request_ctx *request_ctx,
				 struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
		return 0;
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
	if (!auth_api_check(request_ctx)) {
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
/* HTTP service + resource registration                                         */
/* -------------------------------------------------------------------------- */

static uint16_t http_port = CONFIG_HTTP_DASHBOARD_PORT;

HTTP_SERVICE_DEFINE(dashboard_svc, NULL, &http_port, 3, 10, NULL, NULL, NULL);

#if defined(CONFIG_HTTP_DASHBOARD_AUTH)
HTTP_RESOURCE_DEFINE(login_resource, dashboard_svc, "/login", &login_page_detail);
HTTP_RESOURCE_DEFINE(api_login_resource, dashboard_svc, "/api/login", &api_login_detail);
HTTP_RESOURCE_DEFINE(api_logout_resource, dashboard_svc, "/api/logout", &api_logout_detail);
HTTP_RESOURCE_DEFINE(api_change_creds_resource, dashboard_svc, "/api/change-credentials",
		     &api_change_creds_detail);
HTTP_RESOURCE_DEFINE(api_token_rotate_resource, dashboard_svc, "/api/token/rotate",
		     &api_token_rotate_detail);
#endif

HTTP_RESOURCE_DEFINE(root_resource, dashboard_svc, "/", &root_detail);
HTTP_RESOURCE_DEFINE(config_resource, dashboard_svc, "/config", &config_page_detail);
HTTP_RESOURCE_DEFINE(api_data_resource, dashboard_svc, "/api/data", &api_data_detail);
HTTP_RESOURCE_DEFINE(api_config_resource, dashboard_svc, "/api/config", &api_config_detail);
HTTP_RESOURCE_DEFINE(api_locations_resource, dashboard_svc, "/api/locations",
		     &api_locations_detail);

/* -------------------------------------------------------------------------- */
/* SYS_INIT                                                                     */
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

/* -------------------------------------------------------------------------- */
/* Shell commands                                                               */
/* -------------------------------------------------------------------------- */

#if defined(CONFIG_HTTP_DASHBOARD_AUTH) && defined(CONFIG_HTTP_DASHBOARD_AUTH_SHELL)
#	include <zephyr/shell/shell.h>

static int cmd_token_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	char tok[AUTH_SESSION_TOKEN_LEN + 1];

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

	char tok[AUTH_SESSION_TOKEN_LEN + 1];

	auth_token_copy(tok, sizeof(tok));
	shell_print(sh, "New token — Authorization: Bearer %s", tok);
	return 0;
}

static int cmd_user_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	char username[AUTH_CRED_MAX];

	auth_username_copy(username, sizeof(username));
	shell_print(sh, "username: %s", username);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_token, SHELL_CMD(show, NULL, "Print current API bearer token", cmd_token_show),
	SHELL_CMD(rotate, NULL, "Generate and apply a new API token", cmd_token_rotate),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_user,
			       SHELL_CMD(show, NULL, "Print current dashboard username",
					 cmd_user_show),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_http_dashboard,
			       SHELL_CMD(token, &sub_token, "API token management", NULL),
			       SHELL_CMD(user, &sub_user, "User management", NULL),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(http_dashboard, &sub_http_dashboard, "HTTP dashboard commands", NULL);

#endif /* CONFIG_HTTP_DASHBOARD_AUTH && CONFIG_HTTP_DASHBOARD_AUTH_SHELL */

SYS_INIT(http_dashboard_init, APPLICATION, 97);
