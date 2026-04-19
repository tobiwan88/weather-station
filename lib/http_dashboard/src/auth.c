/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include "auth.h"

LOG_MODULE_DECLARE(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Static token storage                                                        */
/* -------------------------------------------------------------------------- */

/*
 * 32 hex chars + NUL.  Written at init or on rotate, read-only during
 * normal operation.  A spinlock guards rotate vs. copy races.
 */
static char s_token[AUTH_TOKEN_STR_LEN + 1];
static struct k_spinlock s_token_lock;

#define BEARER_PREFIX     "Bearer "
#define BEARER_PREFIX_LEN 7U

/* -------------------------------------------------------------------------- */
/* Internal: fill s_token with a fresh CSPRNG value (caller holds no lock)   */
/* -------------------------------------------------------------------------- */

static int generate_token(void)
{
	uint8_t raw[16];
	int rc = sys_csrand_get(raw, sizeof(raw));

	if (rc != 0) {
		LOG_ERR("sys_csrand_get failed: %d", rc);
		return rc;
	}

	/*
	 * bin2hex writes (hexlen - 1) hex chars + NUL. Providing
	 * AUTH_TOKEN_STR_LEN + 1 yields exactly 32 hex chars + NUL.
	 */
	size_t written = bin2hex(raw, sizeof(raw), s_token, sizeof(s_token));

	if (written != AUTH_TOKEN_STR_LEN) {
		LOG_ERR("bin2hex wrote %zu chars, expected %d", written, AUTH_TOKEN_STR_LEN);
		return -EIO;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int auth_init(void)
{
	/*
	 * Validate the default token length at build time.
	 * sizeof(string_literal) includes the NUL terminator, so:
	 *   empty string ""      → sizeof == 1  (first condition passes)
	 *   32-char token        → sizeof == 33 (second condition passes)
	 *   any other length     → BUILD_ASSERT fires
	 */
	BUILD_ASSERT(sizeof(CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN) == 1 ||
			     sizeof(CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN) - 1 ==
				     AUTH_TOKEN_STR_LEN,
		     "HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN must be exactly 32 hex characters");

	/*
	 * sizeof() is a compile-time constant expression; the compiler eliminates
	 * the dead branch entirely.  We use it here (not in #if) because the C
	 * preprocessor cannot evaluate sizeof().
	 */
	if (sizeof(CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN) > 1) {
		strncpy(s_token, CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN, sizeof(s_token) - 1);
		s_token[AUTH_TOKEN_STR_LEN] = '\0';
		LOG_INF("HTTP auth: using compile-time dev token");
		return 0;
	}

	int rc = generate_token();

	if (rc == 0) {
		LOG_INF("HTTP auth: random token generated "
			"(run 'http_dashboard token show' to read it)");
	}
	return rc;
}

/*
 * Constant-time string comparison.
 *
 * Avoids early-exit timing oracles by always iterating for max(alen, blen)
 * iterations, accumulating differences into a single byte.  Returns 0 only
 * when lengths are equal AND every character matches.
 *
 * Zephyr v4.3.0 has no generic timing-safe memcmp at the application layer,
 * so this is hand-rolled.  The token is 32 chars — the loop is short.
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

bool auth_check(const struct http_request_ctx *request_ctx)
{
	if (request_ctx == NULL) {
		LOG_DBG("auth_check: request_ctx is NULL");
		return false;
	}

	LOG_DBG("auth_check: header_count=%zu", request_ctx->header_count);

	for (size_t i = 0; i < request_ctx->header_count; i++) {
		const struct http_header *h = &request_ctx->headers[i];

		LOG_DBG("auth_check: header[%zu] name=%s value=%s", i, h->name ? h->name : "(null)",
			h->value ? h->value : "(null)");

		if (h->name == NULL || h->value == NULL) {
			continue;
		}

		/*
		 * Header capture preserves the casing sent by the client.
		 * Standard HTTP clients send "Authorization" (title-case),
		 * which matches the registered capture name.
		 */
		if (strcmp(h->name, "Authorization") != 0) {
			continue;
		}

		const char *val = h->value;
		size_t vlen = strlen(val);

		/* Must be longer than "Bearer " prefix. */
		if (vlen <= BEARER_PREFIX_LEN) {
			LOG_DBG("auth_check: value too short (%zu)", vlen);
			return false;
		}
		if (strncmp(val, BEARER_PREFIX, BEARER_PREFIX_LEN) != 0) {
			LOG_DBG("auth_check: missing Bearer prefix");
			return false;
		}

		const char *presented = val + BEARER_PREFIX_LEN;
		size_t plen = vlen - BEARER_PREFIX_LEN;

		k_spinlock_key_t key = k_spin_lock(&s_token_lock);
		LOG_DBG("auth_check: comparing token, presented_len=%zu, s_token_len=%d", plen,
			AUTH_TOKEN_STR_LEN);
		LOG_INF("auth_check: comparing presented=%.32s with s_token=%.32s", presented,
			s_token);
		int diff = ct_strcmp(presented, plen, s_token, AUTH_TOKEN_STR_LEN);
		k_spin_unlock(&s_token_lock, key);

		LOG_INF("auth_check: result=%s (diff=%d)", (diff == 0) ? "ok" : "mismatch", diff);
		return (diff == 0);
	}

	LOG_DBG("auth_check: Authorization header not found");
	return false; /* Authorization header not present. */
}

int auth_token_rotate(void)
{
	LOG_DBG("auth_token_rotate: acquiring lock");
	k_spinlock_key_t key = k_spin_lock(&s_token_lock);
	LOG_DBG("auth_token_rotate: lock acquired, generating token");
	int rc = generate_token();

	k_spin_unlock(&s_token_lock, key);
	LOG_DBG("auth_token_rotate: token generated, lock released");

	if (rc == 0) {
		LOG_INF("HTTP auth: token rotated");
	}
	return rc;
}

void auth_token_copy(char *out, size_t len)
{
	if (out == NULL || len == 0) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&s_token_lock);

	strncpy(out, s_token, len - 1);
	out[len - 1] = '\0';
	k_spin_unlock(&s_token_lock, key);
}
