/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HTTP_DASHBOARD_FORM_PARSE_H_
#define HTTP_DASHBOARD_FORM_PARSE_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Decode a URL-encoded string in-place.
 *
 * Converts '+' to space and '%HH' sequences to the corresponding byte.
 * The result is always shorter than or equal to the input.
 */
void url_decode(char *s);

/**
 * @brief Extract a field value from an application/x-www-form-urlencoded body.
 *
 * Searches @p body for an occurrence of @p key '=' value and copies the
 * URL-decoded value into @p out (at most @p out_len - 1 bytes, NUL-terminated).
 *
 * @param body    Raw form-encoded body string (not modified).
 * @param key     Field name to find.
 * @param out     Output buffer for the decoded value.
 * @param out_len Size of @p out in bytes.
 * @return true if the key was found, false otherwise.
 */
bool form_extract(const char *body, const char *key, char *out, size_t out_len);

#endif /* HTTP_DASHBOARD_FORM_PARSE_H_ */
