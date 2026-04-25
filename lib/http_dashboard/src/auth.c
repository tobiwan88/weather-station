/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "auth.h"

LOG_MODULE_DECLARE(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

#define DASH_SUBTREE        "dash"
#define DASH_KEY_USER       "user"
#define DASH_KEY_PASS       "pass"
#define DASH_KEY_TOKEN      "token"
#define DASH_SETTINGS_USER  DASH_SUBTREE "/" DASH_KEY_USER
#define DASH_SETTINGS_PASS  DASH_SUBTREE "/" DASH_KEY_PASS
#define DASH_SETTINGS_TOKEN DASH_SUBTREE "/" DASH_KEY_TOKEN

/* -------------------------------------------------------------------------- */
/* Session table                                                                */
/* -------------------------------------------------------------------------- */

struct session_slot {
	char token[AUTH_SESSION_TOKEN_LEN + 1];
	uint32_t birth_tick; /* k_uptime_get_32() at creation — used for eviction */
	bool active;
};

static struct session_slot s_sessions[CONFIG_HTTP_DASHBOARD_SESSION_COUNT];
static struct k_spinlock s_session_lock;

/* -------------------------------------------------------------------------- */
/* Credentials                                                                  */
/* -------------------------------------------------------------------------- */

static char s_username[AUTH_CRED_MAX];
static char s_password[AUTH_CRED_MAX];
static struct k_spinlock s_cred_lock;

/* -------------------------------------------------------------------------- */
/* API bearer token                                                             */
/* -------------------------------------------------------------------------- */

static char s_api_token[AUTH_SESSION_TOKEN_LEN + 1];
static struct k_spinlock s_api_token_lock;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * Constant-time string comparison.  Iterates max(alen, blen) times regardless
 * of content so there is no timing oracle.  Returns 0 only when lengths are
 * equal AND every byte matches.
 */
static int ct_strcmp(const char *a, size_t alen, const char *b, size_t blen)
{
	size_t maxlen = (alen > blen) ? alen : blen;
	uint8_t diff = (alen != blen) ? 1U : 0U;

	for (size_t i = 0; i < maxlen; i++) {
		uint8_t ca = (i < alen) ? (uint8_t)a[i] : 0U;
		uint8_t cb = (i < blen) ? (uint8_t)b[i] : 0U;

		diff |= ca ^ cb;
	}
	return (int)diff;
}

/* Generate a 32-hex-char CSPRNG token into @p out (must be >= 33 bytes). */
static int generate_token(char *out, size_t out_len)
{
	uint8_t raw[16];
	int rc = sys_csrand_get(raw, sizeof(raw));

	if (rc != 0) {
		return rc;
	}
	size_t written = bin2hex(raw, sizeof(raw), out, out_len);

	if (written != AUTH_SESSION_TOKEN_LEN) {
		return -EIO;
	}
	return 0;
}

/*
 * Extract a cookie field from a raw Cookie header value.
 * Cookie format: "name1=val1; name2=val2; ..."
 * Modelled on form_extract() but uses "; " as the pair separator.
 * Returns true and writes into @p out (NUL-terminated) when the field is found.
 */
static bool cookie_extract(const char *cookie_str, const char *field, char *out, size_t out_len)
{
	if (!cookie_str || !field || !out || out_len == 0) {
		return false;
	}
	size_t flen = strlen(field);
	const char *p = cookie_str;

	while (*p != '\0') {
		/* Skip leading spaces */
		while (*p == ' ') {
			p++;
		}
		/* Check if this pair starts with field= */
		if (strncmp(p, field, flen) == 0 && p[flen] == '=') {
			const char *val = p + flen + 1;
			const char *end = strchr(val, ';');

			if (!end) {
				end = val + strlen(val);
			}
			size_t vlen = (size_t)(end - val);

			if (vlen >= out_len) {
				vlen = out_len - 1;
			}
			memcpy(out, val, vlen);
			out[vlen] = '\0';
			return true;
		}
		/* Advance to next cookie pair (RFC 6265: "; " or ";") */
		const char *sep = strchr(p, ';');

		if (!sep) {
			break;
		}
		p = sep + 1;
		while (*p == ' ') {
			p++;
		}
	}
	return false;
}

/* Extract the session token from request headers (Cookie: session=<hex>).
 * Writes into @p out (must be >= AUTH_SESSION_TOKEN_LEN + 1).
 * Returns true on success.
 */
static bool extract_session_token(const struct http_request_ctx *request_ctx, char *out,
				  size_t out_len)
{
	if (!request_ctx) {
		return false;
	}
	for (size_t i = 0; i < request_ctx->header_count; i++) {
		const struct http_header *h = &request_ctx->headers[i];

		if (!h->name || !h->value) {
			continue;
		}
		if (strcmp(h->name, "Cookie") != 0) {
			continue;
		}
		return cookie_extract(h->value, "session", out, out_len);
	}
	return false;
}

/* -------------------------------------------------------------------------- */
/* Settings handler                                                             */
/* -------------------------------------------------------------------------- */

static int dash_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);

	char buf[AUTH_CRED_MAX];
	ssize_t ret = read_cb(cb_arg, buf, sizeof(buf) - 1);

	if (ret <= 0) {
		return 0;
	}
	buf[ret] = '\0';

	if (strcmp(key, DASH_KEY_USER) == 0) {
		strncpy(s_username, buf, AUTH_CRED_MAX - 1);
		s_username[AUTH_CRED_MAX - 1] = '\0';
	} else if (strcmp(key, DASH_KEY_PASS) == 0) {
		strncpy(s_password, buf, AUTH_CRED_MAX - 1);
		s_password[AUTH_CRED_MAX - 1] = '\0';
	} else if (strcmp(key, DASH_KEY_TOKEN) == 0) {
		strncpy(s_api_token, buf, AUTH_SESSION_TOKEN_LEN);
		s_api_token[AUTH_SESSION_TOKEN_LEN] = '\0';
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(dash, DASH_SUBTREE, NULL, dash_settings_set, NULL, NULL);

/* Load settings subtree before auth_init() runs at APPLICATION 97. */
static int auth_settings_load(void)
{
	return settings_load_subtree(DASH_SUBTREE);
}

SYS_INIT(auth_settings_load, APPLICATION, 96);

/* -------------------------------------------------------------------------- */
/* Public API — init                                                            */
/* -------------------------------------------------------------------------- */

int auth_init(void)
{
	/* Credentials: write defaults on first boot (settings returned empty). */
	if (s_username[0] == '\0') {
		strncpy(s_username, CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_USER, AUTH_CRED_MAX - 1);
		s_username[AUTH_CRED_MAX - 1] = '\0';
		settings_save_one(DASH_SETTINGS_USER, s_username, strlen(s_username) + 1);
		LOG_INF("HTTP auth: first-boot username set to '%s'", s_username);
	}
	if (s_password[0] == '\0') {
		strncpy(s_password, CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_PASS, AUTH_CRED_MAX - 1);
		s_password[AUTH_CRED_MAX - 1] = '\0';
		settings_save_one(DASH_SETTINGS_PASS, s_password, strlen(s_password) + 1);
		LOG_INF("HTTP auth: first-boot password set");
	}

	/* API token: generate on first boot. */
	if (s_api_token[0] == '\0') {
		int rc = generate_token(s_api_token, sizeof(s_api_token));

		if (rc != 0) {
			LOG_ERR("HTTP auth: failed to generate API token: %d", rc);
			return rc;
		}
		settings_save_one(DASH_SETTINGS_TOKEN, s_api_token, AUTH_SESSION_TOKEN_LEN + 1);
		LOG_INF("HTTP auth: API token generated "
			"(run 'http_dashboard token show' to read it)");
	} else {
		LOG_INF("HTTP auth: API token loaded from settings");
	}

	/* Zero the session table. */
	memset(s_sessions, 0, sizeof(s_sessions));
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Browser sessions                                                             */
/* -------------------------------------------------------------------------- */

bool auth_session_check(const struct http_request_ctx *request_ctx)
{
	char presented[AUTH_SESSION_TOKEN_LEN + 1];

	if (!extract_session_token(request_ctx, presented, sizeof(presented))) {
		LOG_DBG("auth_session_check: no session cookie");
		return false;
	}

	size_t plen = strlen(presented);
	k_spinlock_key_t key = k_spin_lock(&s_session_lock);

	for (int i = 0; i < CONFIG_HTTP_DASHBOARD_SESSION_COUNT; i++) {
		if (!s_sessions[i].active) {
			continue;
		}
		if (ct_strcmp(presented, plen, s_sessions[i].token, AUTH_SESSION_TOKEN_LEN) == 0) {
			k_spin_unlock(&s_session_lock, key);
			LOG_DBG("auth_session_check: session valid (slot %d)", i);
			return true;
		}
	}
	k_spin_unlock(&s_session_lock, key);
	LOG_DBG("auth_session_check: no matching session");
	return false;
}

int auth_login(const char *username, const char *password, char *token_out, size_t token_out_len)
{
	if (!username || !password || !token_out || token_out_len < AUTH_SESSION_TOKEN_LEN + 1) {
		return -EINVAL;
	}

	/* Validate credentials constant-time. */
	k_spinlock_key_t ck = k_spin_lock(&s_cred_lock);
	int udiff = ct_strcmp(username, strlen(username), s_username, strlen(s_username));
	int pdiff = ct_strcmp(password, strlen(password), s_password, strlen(s_password));

	k_spin_unlock(&s_cred_lock, ck);

	if (udiff != 0 || pdiff != 0) {
		LOG_DBG("auth_login: bad credentials");
		return -EACCES;
	}

	/* Generate session token. */
	char new_token[AUTH_SESSION_TOKEN_LEN + 1];
	int rc = generate_token(new_token, sizeof(new_token));

	if (rc != 0) {
		return rc;
	}

	/* Find a free slot; evict the oldest if full. */
	k_spinlock_key_t sk = k_spin_lock(&s_session_lock);
	int target = -1;
	uint32_t oldest_tick = UINT32_MAX;
	int oldest_idx = 0;

	for (int i = 0; i < CONFIG_HTTP_DASHBOARD_SESSION_COUNT; i++) {
		if (!s_sessions[i].active) {
			target = i;
			break;
		}
		if (s_sessions[i].birth_tick < oldest_tick) {
			oldest_tick = s_sessions[i].birth_tick;
			oldest_idx = i;
		}
	}
	if (target < 0) {
		target = oldest_idx; /* evict oldest */
	}
	memcpy(s_sessions[target].token, new_token, AUTH_SESSION_TOKEN_LEN + 1);
	s_sessions[target].birth_tick = k_uptime_get_32();
	s_sessions[target].active = true;
	k_spin_unlock(&s_session_lock, sk);

	memcpy(token_out, new_token, AUTH_SESSION_TOKEN_LEN + 1);
	LOG_INF("auth_login: session created (slot %d)", target);
	return 0;
}

void auth_logout(const struct http_request_ctx *request_ctx)
{
	char presented[AUTH_SESSION_TOKEN_LEN + 1];

	if (!extract_session_token(request_ctx, presented, sizeof(presented))) {
		return;
	}
	size_t plen = strlen(presented);
	k_spinlock_key_t key = k_spin_lock(&s_session_lock);

	for (int i = 0; i < CONFIG_HTTP_DASHBOARD_SESSION_COUNT; i++) {
		if (!s_sessions[i].active) {
			continue;
		}
		if (ct_strcmp(presented, plen, s_sessions[i].token, AUTH_SESSION_TOKEN_LEN) == 0) {
			s_sessions[i].active = false;
			memset(s_sessions[i].token, 0, sizeof(s_sessions[i].token));
			LOG_DBG("auth_logout: session invalidated (slot %d)", i);
			break;
		}
	}
	k_spin_unlock(&s_session_lock, key);
}

/* -------------------------------------------------------------------------- */
/* Unified API check: session cookie OR bearer token                           */
/* -------------------------------------------------------------------------- */

bool auth_api_check(const struct http_request_ctx *request_ctx)
{
	/* Try session cookie first. */
	if (auth_session_check(request_ctx)) {
		return true;
	}

	/* Fall back to bearer token. */
	if (!request_ctx) {
		return false;
	}
#define BEARER_PREFIX     "Bearer "
#define BEARER_PREFIX_LEN 7U
	for (size_t i = 0; i < request_ctx->header_count; i++) {
		const struct http_header *h = &request_ctx->headers[i];

		if (!h->name || !h->value) {
			continue;
		}
		if (strcmp(h->name, "Authorization") != 0) {
			continue;
		}
		const char *val = h->value;
		size_t vlen = strlen(val);

		if (vlen <= BEARER_PREFIX_LEN ||
		    strncmp(val, BEARER_PREFIX, BEARER_PREFIX_LEN) != 0) {
			return false;
		}
		const char *presented = val + BEARER_PREFIX_LEN;
		size_t plen = vlen - BEARER_PREFIX_LEN;

		k_spinlock_key_t key = k_spin_lock(&s_api_token_lock);
		int diff = ct_strcmp(presented, plen, s_api_token, AUTH_SESSION_TOKEN_LEN);

		k_spin_unlock(&s_api_token_lock, key);
		LOG_DBG("auth_api_check: bearer %s", (diff == 0) ? "ok" : "mismatch");
		return (diff == 0);
	}
	return false;
#undef BEARER_PREFIX
#undef BEARER_PREFIX_LEN
}

/* -------------------------------------------------------------------------- */
/* Credentials management                                                       */
/* -------------------------------------------------------------------------- */

int auth_change_credentials(const char *old_user, const char *old_pass, const char *new_user,
			    const char *new_pass)
{
	if (!old_user || !old_pass || !new_user || !new_pass) {
		return -EINVAL;
	}
	if (strlen(new_user) == 0 || strlen(new_user) >= AUTH_CRED_MAX) {
		return -EINVAL;
	}
	if (strlen(new_pass) == 0 || strlen(new_pass) >= AUTH_CRED_MAX) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&s_cred_lock);
	int udiff = ct_strcmp(old_user, strlen(old_user), s_username, strlen(s_username));
	int pdiff = ct_strcmp(old_pass, strlen(old_pass), s_password, strlen(s_password));

	if (udiff != 0 || pdiff != 0) {
		k_spin_unlock(&s_cred_lock, key);
		return -EACCES;
	}
	strncpy(s_username, new_user, AUTH_CRED_MAX - 1);
	s_username[AUTH_CRED_MAX - 1] = '\0';
	strncpy(s_password, new_pass, AUTH_CRED_MAX - 1);
	s_password[AUTH_CRED_MAX - 1] = '\0';
	k_spin_unlock(&s_cred_lock, key);

	settings_save_one(DASH_SETTINGS_USER, new_user, strlen(new_user) + 1);
	settings_save_one(DASH_SETTINGS_PASS, new_pass, strlen(new_pass) + 1);
	LOG_INF("HTTP auth: credentials updated");
	return 0;
}

void auth_username_copy(char *out, size_t len)
{
	if (!out || len == 0) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&s_cred_lock);

	strncpy(out, s_username, len - 1);
	out[len - 1] = '\0';
	k_spin_unlock(&s_cred_lock, key);
}

/* -------------------------------------------------------------------------- */
/* API token management                                                         */
/* -------------------------------------------------------------------------- */

int auth_token_rotate(void)
{
	char new_token[AUTH_SESSION_TOKEN_LEN + 1];
	int rc = generate_token(new_token, sizeof(new_token));

	if (rc != 0) {
		return rc;
	}
	k_spinlock_key_t key = k_spin_lock(&s_api_token_lock);

	memcpy(s_api_token, new_token, AUTH_SESSION_TOKEN_LEN + 1);
	k_spin_unlock(&s_api_token_lock, key);
	settings_save_one(DASH_SETTINGS_TOKEN, new_token, AUTH_SESSION_TOKEN_LEN + 1);
	LOG_INF("HTTP auth: API token rotated");
	return 0;
}

void auth_token_copy(char *out, size_t len)
{
	if (!out || len == 0) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&s_api_token_lock);

	strncpy(out, s_api_token, len - 1);
	out[len - 1] = '\0';
	k_spin_unlock(&s_api_token_lock, key);
}
