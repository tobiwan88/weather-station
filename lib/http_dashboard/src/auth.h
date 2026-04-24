/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file auth.h
 * @brief Authentication for the HTTP dashboard.
 *
 * Two parallel mechanisms:
 *   - Browser users: username/password login form → HttpOnly session cookie.
 *   - Automation clients: persisted API bearer token via Authorization header.
 *
 * Credentials (username + password) are stored in Zephyr settings under
 * dash/user and dash/pass.  The API token is stored under dash/token.
 * All state is statically allocated; no heap is used.
 *
 * Only compiled when CONFIG_HTTP_DASHBOARD_AUTH=y.
 */
#ifndef HTTP_DASHBOARD_AUTH_H_
#define HTTP_DASHBOARD_AUTH_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/net/http/server.h>

/** Length of a session or API token string (32 hex chars, no NUL). */
#define AUTH_SESSION_TOKEN_LEN 32

/** Kept as alias so shell code that references AUTH_TOKEN_STR_LEN still builds. */
#define AUTH_TOKEN_STR_LEN AUTH_SESSION_TOKEN_LEN

/** Max length of username or password (including NUL terminator). */
#define AUTH_CRED_MAX 65

/**
 * @brief Initialise auth: check settings buffers, write first-boot defaults.
 *
 * Must be called after settings_load_subtree("dash") has completed.
 * Called by http_dashboard_init() via SYS_INIT at APPLICATION 97.
 */
int auth_init(void);

/* -------------------------------------------------------------------------- */
/* Browser session (cookie-based)                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Validate the session cookie in the request headers.
 *
 * Scans request_ctx->headers[] for "Cookie", extracts the "session" field,
 * and compares it constant-time against all active session slots.
 *
 * @return true  if a matching active session was found.
 * @return false otherwise.
 */
bool auth_session_check(const struct http_request_ctx *request_ctx);

/**
 * @brief Validate username+password and create a new browser session.
 *
 * On success, writes a 32-hex-char session token into @p token_out.
 * Evicts the oldest slot if the table is full.
 *
 * @return 0 on success.
 * @return -EACCES on wrong credentials.
 * @return negative errno on CSPRNG failure.
 */
int auth_login(const char *username, const char *password, char *token_out, size_t token_out_len);

/**
 * @brief Invalidate the session identified by the cookie in the request.
 *
 * No-op if no matching session is found.
 */
void auth_logout(const struct http_request_ctx *request_ctx);

/* -------------------------------------------------------------------------- */
/* API endpoints: session cookie OR bearer token                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Check whether the request carries a valid session cookie or API token.
 *
 * Tries auth_session_check() first, then checks the Authorization header
 * against the persisted API bearer token.
 *
 * @return true  if either check passes.
 * @return false otherwise.
 */
bool auth_api_check(const struct http_request_ctx *request_ctx);

/* -------------------------------------------------------------------------- */
/* Credentials management                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Change dashboard username and password.
 *
 * Verifies @p old_user and @p old_pass constant-time before applying the
 * change and persisting the new values via settings_save_one().
 *
 * @return 0 on success.
 * @return -EACCES if old credentials do not match.
 */
int auth_change_credentials(const char *old_user, const char *old_pass, const char *new_user,
			    const char *new_pass);

/**
 * @brief Copy the current username into @p out (NUL-terminated, spinlock-safe).
 */
void auth_username_copy(char *out, size_t len);

/* -------------------------------------------------------------------------- */
/* API token (bearer token for automation clients)                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Replace the API token with a fresh CSPRNG value and persist it.
 *
 * @return 0 on success, negative errno on failure.
 */
int auth_token_rotate(void);

/**
 * @brief Copy the current API token into @p out (NUL-terminated, spinlock-safe).
 *
 * @param out  Must be >= AUTH_SESSION_TOKEN_LEN + 1 bytes.
 */
void auth_token_copy(char *out, size_t len);

#endif /* HTTP_DASHBOARD_AUTH_H_ */
