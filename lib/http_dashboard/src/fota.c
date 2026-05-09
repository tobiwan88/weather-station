/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "auth.h"
#include "fota.h"

LOG_MODULE_DECLARE(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

/* ── shared response headers ─────────────────────────────────────────────── */

static const struct http_header json_ct_hdr[] = {
	{.name = "Content-Type", .value = "application/json"},
};

static const char unauth_body[] = "{\"error\":\"unauthorized\"}";

static void respond_401(struct http_response_ctx *rsp)
{
	rsp->status = HTTP_401_UNAUTHORIZED;
	rsp->headers = json_ct_hdr;
	rsp->header_count = ARRAY_SIZE(json_ct_hdr);
	rsp->body = (const uint8_t *)unauth_body;
	rsp->body_len = sizeof(unauth_body) - 1;
	rsp->final_chunk = true;
}

/* ── POST /api/fota/upload ───────────────────────────────────────────────── */

static struct flash_img_context fota_img_ctx;
static atomic_t fota_in_progress = ATOMIC_INIT(0);
static bool fota_auth_ok;

int fota_upload_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		atomic_set(&fota_in_progress, 0);
		fota_auth_ok = false;
		return 0;
	}

	/* First invocation for this request: auth + flash init. */
	if (!fota_auth_ok && !atomic_get(&fota_in_progress)) {
		if (!auth_api_check(request_ctx)) {
			if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
				respond_401(response_ctx);
			}
			return 0;
		}
		if (!atomic_cas(&fota_in_progress, 0, 1)) {
			/* Another upload already in progress. */
			if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
				static const char busy[] = "{\"error\":\"upload in progress\"}";
				response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->body = (const uint8_t *)busy;
				response_ctx->body_len = sizeof(busy) - 1;
				response_ctx->final_chunk = true;
			}
			return 0;
		}
		int rc = flash_img_init_id(&fota_img_ctx, FIXED_PARTITION_ID(slot1_partition));
		if (rc != 0) {
			LOG_ERR("flash_img_init_id failed: %d", rc);
			atomic_set(&fota_in_progress, 0);
			if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->final_chunk = true;
			}
			return 0;
		}
		fota_auth_ok = true;
	}

	if (!fota_auth_ok) {
		/* Auth failed on an earlier chunk — discard remaining data. */
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
			respond_401(response_ctx);
		}
		return 0;
	}

	/* Stream chunk into flash. */
	bool last = (status == HTTP_SERVER_REQUEST_DATA_FINAL);

	if (request_ctx->data_len > 0 || last) {
		int rc = flash_img_buffered_write(&fota_img_ctx, request_ctx->data,
						  request_ctx->data_len, last);
		if (rc != 0) {
			LOG_ERR("flash_img_buffered_write failed: %d", rc);
			atomic_set(&fota_in_progress, 0);
			fota_auth_ok = false;
			if (last) {
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->final_chunk = true;
			}
			return 0;
		}
	}

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		size_t written = flash_img_bytes_written(&fota_img_ctx);

		LOG_INF("FOTA upload complete: %zu bytes written to slot 1", written);
		atomic_set(&fota_in_progress, 0);
		fota_auth_ok = false;

		static uint8_t ok_buf[64];

		snprintf((char *)ok_buf, sizeof(ok_buf), "{\"ok\":true,\"bytes\":%zu}", written);
		response_ctx->status = HTTP_200_OK;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = ok_buf;
		response_ctx->body_len = strlen((char *)ok_buf);
		response_ctx->final_chunk = true;
	}
	return 0;
}

/* ── POST /api/fota/apply ────────────────────────────────────────────────── */

static struct k_work_delayable fota_reboot_work;

static void fota_reboot_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("FOTA: rebooting to apply update");
	sys_reboot(SYS_REBOOT_COLD);
}

int fota_apply_handler(struct http_client_ctx *client, enum http_transaction_status status,
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

	if (!auth_api_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}

	int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);

	if (rc != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", rc);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	LOG_INF("FOTA: image marked pending — rebooting in 2 s");
	k_work_schedule(&fota_reboot_work, K_MSEC(2000));

	static const char ok[] = "{\"ok\":true,\"rebooting\":true}";

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = (const uint8_t *)ok;
	response_ctx->body_len = sizeof(ok) - 1;
	response_ctx->final_chunk = true;
	return 0;
}

/* ── GET /api/fota/status ────────────────────────────────────────────────── */

int fota_status_handler(struct http_client_ctx *client, enum http_transaction_status status,
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

	if (!auth_api_check(request_ctx)) {
		respond_401(response_ctx);
		return 0;
	}

	struct mcuboot_img_header hdr0 = {0};
	struct mcuboot_img_header hdr1 = {0};

	boot_read_bank_header(FIXED_PARTITION_ID(slot0_partition), &hdr0, sizeof(hdr0));
	boot_read_bank_header(FIXED_PARTITION_ID(slot1_partition), &hdr1, sizeof(hdr1));

	bool confirmed = boot_is_img_confirmed();
	int swap = mcuboot_swap_type();
	bool pending = (swap == BOOT_SWAP_TYPE_TEST || swap == BOOT_SWAP_TYPE_REVERT);

	static uint8_t status_buf[256];
	int len = snprintf((char *)status_buf, sizeof(status_buf),
			   "{"
			   "\"slot0\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":%s},"
			   "\"slot1\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":false},"
			   "\"pending\":%s"
			   "}",
			   hdr0.h.v1.sem_ver.major, hdr0.h.v1.sem_ver.minor,
			   hdr0.h.v1.sem_ver.revision, (unsigned)hdr0.h.v1.sem_ver.build_num,
			   confirmed ? "true" : "false", hdr1.h.v1.sem_ver.major,
			   hdr1.h.v1.sem_ver.minor, hdr1.h.v1.sem_ver.revision,
			   (unsigned)hdr1.h.v1.sem_ver.build_num, pending ? "true" : "false");

	if (len <= 0 || (size_t)len >= sizeof(status_buf)) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->final_chunk = true;
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = json_ct_hdr;
	response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
	response_ctx->body = status_buf;
	response_ctx->body_len = (size_t)len;
	response_ctx->final_chunk = true;
	return 0;
}

/* ── init ────────────────────────────────────────────────────────────────── */

void fota_init(void)
{
	k_work_init_delayable(&fota_reboot_work, fota_reboot_fn);
}
