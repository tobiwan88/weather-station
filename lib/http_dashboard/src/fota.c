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
/* Single source of truth for the upload-busy error body (used in two response
 * paths — avoids duplicating the literal).
 */
static const char upload_busy_body[] = "{\"error\":\"upload in progress\"}";

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
/* Serialises reads and writes of the non-atomic upload-state variables below.
 * The HTTP server is single-threaded, but the ABORTED/COMPLETE callback may
 * fire from a different execution context than the DATA callbacks on some
 * platforms, so explicit locking makes the state machine race-free.
 */
static struct k_spinlock fota_upload_lock;
static bool fota_auth_ok;
static struct http_client_ctx *fota_authorized_client;
static bool fota_write_failed;
static size_t fota_upload_bytes;

int fota_upload_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		K_SPINLOCK(&fota_upload_lock)
		{
			atomic_set(&fota_in_progress, 0);
			fota_auth_ok = false;
			fota_authorized_client = NULL;
			fota_write_failed = false;
			fota_upload_bytes = 0;
		}
		return 0;
	}

	if (fota_write_failed) {
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL ||
		    status == HTTP_SERVER_TRANSACTION_ABORTED ||
		    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
			if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->final_chunk = true;
			}
			fota_write_failed = false;
		}
		return 0;
	}

	/* Reject data from any client that is not the authorized uploader. */
	if (fota_auth_ok && client != fota_authorized_client) {
		/* Different client injecting data into an in-progress upload — drop it. */
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
			response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
			response_ctx->headers = json_ct_hdr;
			response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
			response_ctx->body = (const uint8_t *)upload_busy_body;
			response_ctx->body_len = sizeof(upload_busy_body) - 1;
			response_ctx->final_chunk = true;
		}
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
				response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->body = (const uint8_t *)upload_busy_body;
				response_ctx->body_len = sizeof(upload_busy_body) - 1;
				response_ctx->final_chunk = true;
			}
			return 0;
		}
		fota_upload_bytes = 0;
		int rc = flash_img_init_id(&fota_img_ctx, FIXED_PARTITION_ID(slot1_partition));
		if (rc != 0) {
			LOG_ERR("flash_img_init_id failed: %d", rc);
			/* Only arm the drain flag when there are more chunks coming. */
			K_SPINLOCK(&fota_upload_lock)
			{
				atomic_set(&fota_in_progress, 0);
				fota_auth_ok = false;
				fota_write_failed = (status != HTTP_SERVER_REQUEST_DATA_FINAL);
			}
			if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->final_chunk = true;
			}
			return 0;
		}
		K_SPINLOCK(&fota_upload_lock)
		{
			fota_auth_ok = true;
			fota_authorized_client = client;
		}
	}

	if (!fota_auth_ok) {
		/* Auth failed on an earlier chunk — discard remaining data. */
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
			respond_401(response_ctx);
		}
		return 0;
	}

	/* Stream chunk into flash.
	 * NOTE: flash_img_buffered_write() flushes its internal buffer in
	 * CONFIG_IMG_BLOCK_BUF_SIZE-sized blocks.  With IMG_ERASE_PROGRESSIVELY
	 * each flush may trigger a NOR sector erase (30-400 ms on real hardware),
	 * blocking the HTTP server thread and stalling all other HTTP clients for
	 * that duration.  CONFIG_HTTP_SERVER_STACK_SIZE must be large enough for
	 * the full flash_img → stream_flash → flash_area call chain.
	 */
	bool last = (status == HTTP_SERVER_REQUEST_DATA_FINAL);

	if (request_ctx->data_len > 0 || last) {
		int rc = flash_img_buffered_write(&fota_img_ctx, request_ctx->data,
						  request_ctx->data_len, last);
		if (rc != 0) {
			LOG_ERR("flash_img_buffered_write failed: %d", rc);
			K_SPINLOCK(&fota_upload_lock)
			{
				atomic_set(&fota_in_progress, 0);
				fota_auth_ok = false;
				fota_write_failed = !last;
				fota_upload_bytes = 0;
			}
			if (last) {
				response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->final_chunk = true;
			}
			return 0;
		}
	}

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		size_t written = flash_img_bytes_written(&fota_img_ctx);

		LOG_INF("FOTA upload complete: %zu bytes written to slot 1", written);
		K_SPINLOCK(&fota_upload_lock)
		{
			fota_upload_bytes = written;
			atomic_set(&fota_in_progress, 0);
			fota_auth_ok = false;
		}

		/* ok_buf is protected by the fota_in_progress==0 invariant above:
		 * no concurrent upload can enter the flash-write path until a new
		 * CAS on fota_in_progress succeeds, which cannot happen until after
		 * this response is fully sent by the single HTTP server thread.
		 */
		/* Worst-case: {"ok":true,"bytes":4294967295} = 30 chars. */
		static uint8_t ok_buf[32];

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

static void fota_reboot_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("FOTA: rebooting to apply update");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(fota_reboot_work, fota_reboot_fn);

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

	if (fota_upload_bytes == 0) {
		static const char no_image[] = "{\"error\":\"no image uploaded\"}";

		response_ctx->status = HTTP_409_CONFLICT;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = (const uint8_t *)no_image;
		response_ctx->body_len = sizeof(no_image) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}

	/* Sanity-check the slot 1 image header before marking it pending.
	 * MCUboot validates the full signature at boot, but a corrupt or
	 * truncated upload (fota_upload_bytes > 0 but header unreadable)
	 * would cause a failed boot + automatic revert — a DoS for any
	 * authenticated attacker.  If the header is unreadable, refuse now.
	 */
	struct mcuboot_img_header slot1_hdr = {0};
	int hrc = boot_read_bank_header(FIXED_PARTITION_ID(slot1_partition), &slot1_hdr,
					sizeof(slot1_hdr));

	if (hrc != 0) {
		LOG_ERR("slot1 header unreadable (rc=%d) — refusing apply", hrc);
		static const char bad_img[] = "{\"error\":\"slot1 image header invalid\"}";

		response_ctx->status = HTTP_409_CONFLICT;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = (const uint8_t *)bad_img;
		response_ctx->body_len = sizeof(bad_img) - 1;
		response_ctx->final_chunk = true;
		return 0;
	}

	int rc = boot_request_upgrade(BOOT_UPGRADE_TEST);

	if (rc != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", rc);
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
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

/* The Zephyr HTTP server is single-threaded: no two handler invocations can
 * overlap, so status_buf needs no lock.  Size is worst-case JSON length:
 * {"slot0":{"version":"255.255.65535+4294967295","confirmed":true},
 *  "slot1":{"version":"255.255.65535+4294967295","confirmed":false},
 *  "pending":true} = 148 chars; 160 gives 12 bytes headroom.
 */
static uint8_t status_buf[160];

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

	int rc0 = boot_read_bank_header(FIXED_PARTITION_ID(slot0_partition), &hdr0, sizeof(hdr0));
	int rc1 = boot_read_bank_header(FIXED_PARTITION_ID(slot1_partition), &hdr1, sizeof(hdr1));

	if (rc0 != 0) {
		LOG_WRN("slot0 header unreadable: %d", rc0);
	}
	if (rc1 != 0) {
		LOG_WRN("slot1 header unreadable: %d", rc1);
	}

	bool confirmed = boot_is_img_confirmed();
	int swap = mcuboot_swap_type();

	if (swap < 0) {
		LOG_WRN("mcuboot_swap_type failed: %d", swap);
	}
	bool pending = (swap == BOOT_SWAP_TYPE_TEST || swap == BOOT_SWAP_TYPE_REVERT);

	int len;
	bool ok = false;

	len = snprintf((char *)status_buf, sizeof(status_buf),
		       "{"
		       "\"slot0\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":%s},"
		       "\"slot1\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":false},"
		       "\"pending\":%s"
		       "}",
		       hdr0.h.v1.sem_ver.major, hdr0.h.v1.sem_ver.minor, hdr0.h.v1.sem_ver.revision,
		       (unsigned)hdr0.h.v1.sem_ver.build_num, confirmed ? "true" : "false",
		       hdr1.h.v1.sem_ver.major, hdr1.h.v1.sem_ver.minor, hdr1.h.v1.sem_ver.revision,
		       (unsigned)hdr1.h.v1.sem_ver.build_num, pending ? "true" : "false");

	if (len > 0 && (size_t)len < sizeof(status_buf)) {
		response_ctx->status = HTTP_200_OK;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->body = status_buf;
		response_ctx->body_len = (size_t)len;
		response_ctx->final_chunk = true;
		ok = true;
	}

	if (!ok) {
		response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
		response_ctx->headers = json_ct_hdr;
		response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
		response_ctx->final_chunk = true;
	}
	return 0;
}
