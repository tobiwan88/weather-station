/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <zephyr/net/http/server.h>

/**
 * Initialise the FOTA subsystem (registers the delayed reboot work item).
 * Called from http_dashboard_init().
 */
void fota_init(void);

int fota_upload_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data);

int fota_apply_handler(struct http_client_ctx *client, enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data);

int fota_status_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data);
