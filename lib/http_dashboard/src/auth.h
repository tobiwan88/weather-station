/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file auth.h
 * @brief Bearer token authentication for the HTTP dashboard.
 *
 * All state is statically allocated. The active token is a 32-character
 * lowercase hex string (128 bits of CSPRNG entropy) held in a spinlock-
 * protected buffer.  No sessions, no TLS, no dynamic memory.
 *
 * Only compiled when CONFIG_HTTP_DASHBOARD_AUTH=y.
 */
#ifndef HTTP_DASHBOARD_AUTH_H_
#define HTTP_DASHBOARD_AUTH_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/net/http/server.h>

/** Length of the hex token string (32 chars, no NUL). */
#define AUTH_TOKEN_STR_LEN 32

/**
 * @brief Initialise the auth subsystem and set the active token.
 *
 * If CONFIG_HTTP_DASHBOARD_AUTH_DEFAULT_TOKEN is non-empty, copies it as the
 * active token (a BUILD_ASSERT ensures it is exactly 32 chars). Otherwise
 * calls sys_csrand_get() to fill 16 random bytes and formats them as hex.
 *
 * Must be called before any HTTP handler runs. Called by http_dashboard_init()
 * via SYS_INIT at APPLICATION 97.
 *
 * @return 0 on success, negative errno from sys_csrand_get() on failure.
 */
int auth_init(void);

/**
 * @brief Validate the Authorization header from an HTTP request context.
 *
 * Scans request_ctx->headers[] for the "Authorization" header (captured via
 * HTTP_SERVER_REGISTER_HEADER_CAPTURE). Accepts the value only when it matches
 * "Bearer <active_token>" exactly, using a constant-time loop.
 *
 * All validation is defensive (if/return) — this function never asserts on
 * external input.
 *
 * @param request_ctx  HTTP request context provided by the handler callback.
 * @return true   header present and token matches.
 * @return false  header absent, malformed, or token mismatch.
 */
bool auth_check(const struct http_request_ctx *request_ctx);

/**
 * @brief Replace the active token with a new CSPRNG value.
 *
 * Thread-safe: protected by the same spinlock used by auth_check(). Safe to
 * call from the shell thread while HTTP handlers are running concurrently.
 *
 * @return 0 on success, negative errno on sys_csrand_get() failure.
 */
int auth_token_rotate(void);

/**
 * @brief Copy the current token string into @p out (NUL-terminated).
 *
 * @param out  Destination buffer (must be >= AUTH_TOKEN_STR_LEN + 1 bytes).
 * @param len  Size of @p out.
 */
void auth_token_copy(char *out, size_t len);

#endif /* HTTP_DASHBOARD_AUTH_H_ */
