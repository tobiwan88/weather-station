/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "form_parse.h"

void url_decode(char *s)
{
	char *rd = s, *wr = s;

	while (*rd) {
		if (*rd == '+') {
			*wr++ = ' ';
			rd++;
		} else if (*rd == '%' && rd[1] && rd[2]) {
			char hex[3] = {rd[1], rd[2], '\0'};

			*wr++ = (char)strtol(hex, NULL, 16);
			rd += 3;
		} else {
			*wr++ = *rd++;
		}
	}
	*wr = '\0';
}

bool form_extract(const char *body, const char *key, char *out, size_t out_len)
{
	size_t klen = strlen(key);
	const char *p = body;

	while (p && *p) {
		if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
			const char *v = p + klen + 1;
			const char *end = strchr(v, '&');
			size_t vlen = end ? (size_t)(end - v) : strlen(v);

			if (vlen >= out_len) {
				vlen = out_len - 1;
			}
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			url_decode(out);
			return true;
		}
		p = strchr(p, '&');
		if (p) {
			p++;
		}
	}
	return false;
}
