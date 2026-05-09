# MCUboot FOTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add MCUboot-based firmware update to the `frdm_mcxn947` gateway — UART via MCUmgr SMP and HTTP upload via the dashboard — with ED25519 image signing, A/B swap, and automatic rollback.

**Architecture:** MCUboot is added as a sysbuild bootloader alongside the gateway app. Key material never enters the repo — a local script generates a dev key in a gitignored directory. Three new HTTP endpoints stream image data directly to flash (no full-image RAM buffer) and trigger MCUboot's test/confirm lifecycle. A `fota_confirm` library auto-confirms a good boot after a 5-second settle delay. A stub `fota_relay` header reserves the interface for future wireless relay.

**Tech Stack:** Zephyr 4.4 sysbuild, MCUboot, `imgtool` (part of MCUboot west module), Zephyr `flash_img` API, `zephyr/dfu/mcuboot.h`, Zephyr HTTP server dynamic handlers.

---

## File map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `scripts/gen-dev-key.sh` | Generate `keys/dev-ed25519.pem` if absent |
| Modify | `.gitignore` | Exclude `keys/` directory |
| Create | `apps/gateway/sysbuild.conf` | Enable MCUboot + ED25519 for hw builds |
| Create | `apps/gateway/sysbuild/mcuboot.conf` | MCUboot Kconfig (swap mode, log level) |
| Modify | `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf` | MCUmgr Kconfig + enable FOTA + confirm libs |
| Create | `lib/fota_relay/include/fota_relay/fota_relay.h` | Stub vtable — future relay transport interface |
| Create | `lib/fota_relay/CMakeLists.txt` | Header-only lib |
| Create | `lib/fota_relay/Kconfig` | `CONFIG_FOTA_RELAY` |
| Modify | `lib/http_dashboard/Kconfig` | Add `CONFIG_HTTP_DASHBOARD_FOTA` |
| Create | `lib/http_dashboard/src/fota.h` | Internal header — handler function declarations |
| Create | `lib/http_dashboard/src/fota.c` | Upload / apply / status handlers + flash_img streaming |
| Modify | `lib/http_dashboard/CMakeLists.txt` | Compile `fota.c` when `CONFIG_HTTP_DASHBOARD_FOTA` |
| Modify | `lib/http_dashboard/src/http_dashboard.c` | Register 3 FOTA resources + call `fota_init()` |
| Create | `lib/fota_confirm/Kconfig` | `CONFIG_FOTA_CONFIRM` + `CONFIG_FOTA_CONFIRM_DELAY_S` |
| Create | `lib/fota_confirm/CMakeLists.txt` | `zephyr_library()` for `fota_confirm.c` |
| Create | `lib/fota_confirm/src/fota_confirm.c` | `SYS_INIT APPLICATION 99` — auto-confirm after settle |
| Modify | `CMakeLists.txt` | `add_subdirectory_ifdef` for `fota_relay` + `fota_confirm` |
| Modify | `lib/Kconfig` | `rsource` for `fota_relay` + `fota_confirm` |

---

## Task 1: Key generation script and .gitignore

**Files:**
- Create: `scripts/gen-dev-key.sh`
- Modify: `.gitignore`

- [ ] **Step 1: Add `keys/` to .gitignore**

Read the current `.gitignore` first, then append:

```
keys/
```

Run:
```bash
echo "keys/" >> /home/zephyr/workspace/weather-station/.gitignore
```

- [ ] **Step 2: Create `scripts/gen-dev-key.sh`**

```bash
mkdir -p /home/zephyr/workspace/weather-station/scripts
```

Write `scripts/gen-dev-key.sh`:

```bash
#!/usr/bin/env bash
# Generate a local ED25519 signing key for MCUboot.
# The private key is stored in keys/ which is gitignored — never commit it.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
KEY_FILE="${REPO_ROOT}/keys/dev-ed25519.pem"

mkdir -p "${REPO_ROOT}/keys"

if [ -f "${KEY_FILE}" ]; then
    echo "Key already exists at ${KEY_FILE} — skipping generation."
    exit 0
fi

imgtool keygen -k "${KEY_FILE}" -t ed25519
echo "Dev key generated: ${KEY_FILE}"
echo "This file is gitignored. Never commit it."
```

- [ ] **Step 3: Make script executable**

```bash
chmod +x /home/zephyr/workspace/weather-station/scripts/gen-dev-key.sh
```

- [ ] **Step 4: Verify the script runs and produces a key**

```bash
cd /home/zephyr/workspace/weather-station
./scripts/gen-dev-key.sh
```

Expected output:
```
Dev key generated: /home/zephyr/workspace/weather-station/keys/dev-ed25519.pem
```

Run again — must be idempotent:
```bash
./scripts/gen-dev-key.sh
```

Expected output:
```
Key already exists at .../keys/dev-ed25519.pem — skipping generation.
```

- [ ] **Step 5: Verify key is gitignored**

```bash
cd /home/zephyr/workspace/weather-station
git status keys/
```

Expected: `keys/` does not appear as an untracked file (it is ignored).

- [ ] **Step 6: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add scripts/gen-dev-key.sh .gitignore
git commit -m "chore(fota): add key generation script and gitignore keys directory"
```

---

## Task 2: Sysbuild configuration for MCUboot

**Files:**
- Create: `apps/gateway/sysbuild.conf`
- Create: `apps/gateway/sysbuild/mcuboot.conf`

- [ ] **Step 1: Create the sysbuild directory**

```bash
mkdir -p /home/zephyr/workspace/weather-station/apps/gateway/sysbuild
```

- [ ] **Step 2: Create `apps/gateway/sysbuild.conf`**

This file activates MCUboot for all hardware builds of the gateway app. It is loaded by sysbuild before board-specific fragments.

```ini
# apps/gateway/sysbuild.conf
# Enable MCUboot bootloader. Key file and build details are in sysbuild/mcuboot.conf.
# Sign type is set here; key path is passed at build time via
#   -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=<absolute-path>
SB_CONFIG_BOOTLOADER_MCUBOOT=y
SB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y
```

- [ ] **Step 3: Create `apps/gateway/sysbuild/mcuboot.conf`**

MCUboot Kconfig that applies only to the MCUboot image (not the gateway app image).

```ini
# apps/gateway/sysbuild/mcuboot.conf
CONFIG_MCUBOOT_LOG_LEVEL_WRN=y
CONFIG_BOOT_SWAP_USING_MOVE=y
CONFIG_BOOT_UPGRADE_ONLY=n
CONFIG_BOOT_BOOTSTRAP=n
CONFIG_BOOT_MAX_IMG_SECTORS=256
CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=n
```

- [ ] **Step 4: Verify native_sim build is unaffected**

Native_sim uses `west build` without `--sysbuild`, so `sysbuild.conf` is ignored.

```bash
cd /home/zephyr/workspace/weather-station
/build-and-test
```

Expected: all tests pass, no new errors.

- [ ] **Step 5: Verify sysbuild CMake configure succeeds for hardware**

This only runs CMake configuration (no flashing required). The key file defaults to MCUboot's bundled test key when not specified; this is acceptable for a build-only check.

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
west build apps/gateway \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  --cmake-only \
  -d build/frdm_mcuboot_check
```

Expected: CMake configures without error. MCUboot and gateway images are both listed in sysbuild output.

- [ ] **Step 6: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add apps/gateway/sysbuild.conf apps/gateway/sysbuild/mcuboot.conf
git commit -m "feat(fota): add sysbuild config for MCUboot on frdm_mcxn947"
```

---

## Task 3: MCUmgr Kconfig additions to hardware board config

**Files:**
- Modify: `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf`

- [ ] **Step 1: Append MCUmgr and image management Kconfig**

Add to the end of `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf`:

```ini
# ---------------------------------------------------------------
# MCUmgr — SMP protocol over UART for firmware update
# ---------------------------------------------------------------
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_UART=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_IMG_MANAGER=y
CONFIG_STREAM_FLASH=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y

# MCUmgr + HTTP server share the epoll fd — increase poll slots
CONFIG_ZVFS_POLL_MAX=12

# MCUmgr SMP requires ZCBOR and CRC
CONFIG_ZCBOR=y
CONFIG_CRC=y
```

- [ ] **Step 2: Verify native_sim build still passes**

```bash
/build-and-test
```

Expected: all tests pass (none of these Kconfig symbols affect native_sim).

- [ ] **Step 3: Verify hardware build compiles**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
west build apps/gateway \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  -d build/frdm_mcuboot_check
```

Expected: both MCUboot and gateway images compile without error.

- [ ] **Step 4: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf
git commit -m "feat(fota): add MCUmgr Kconfig to frdm_mcxn947 board config"
```

---

## Task 4: FOTA relay stub header

**Files:**
- Create: `lib/fota_relay/include/fota_relay/fota_relay.h`
- Create: `lib/fota_relay/CMakeLists.txt`
- Create: `lib/fota_relay/Kconfig`
- Modify: `CMakeLists.txt`
- Modify: `lib/Kconfig`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p /home/zephyr/workspace/weather-station/lib/fota_relay/include/fota_relay
```

- [ ] **Step 2: Create `lib/fota_relay/include/fota_relay/fota_relay.h`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Transport vtable for gateway-to-sensor-node firmware relay.
 *
 * Implement one instance per wireless transport (LoRa, BLE, …) using
 * this interface. Implementation is deferred — see ADR-014.
 *
 * @note The gateway calls upload() in chunks; the transport is responsible
 *       for fragmentation, duty-cycle management, and ACK/retry.
 */
struct fota_relay_transport_api {
	/**
	 * @brief Upload a firmware chunk to a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to update.
	 * @param data        Pointer to image chunk (may be NULL when len==0).
	 * @param len         Length of this chunk in bytes.
	 * @param offset      Byte offset of this chunk in the full image.
	 * @param last        True if this is the final chunk.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*upload)(uint32_t target_uid, const uint8_t *data, size_t len,
		      size_t offset, bool last);

	/**
	 * @brief Query the running firmware version on a target sensor node.
	 *
	 * @param target_uid  Sensor UID of the node to query.
	 * @param buf         Output buffer for version string (e.g. "1.2.3+0").
	 * @param len         Size of buf including NUL terminator.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*get_version)(uint32_t target_uid, char *buf, size_t len);
};
```

- [ ] **Step 3: Create `lib/fota_relay/CMakeLists.txt`**

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FOTA_RELAY)
  zephyr_include_directories(include)
endif()
```

- [ ] **Step 4: Create `lib/fota_relay/Kconfig`**

```kconfig
# SPDX-License-Identifier: Apache-2.0

config FOTA_RELAY
	bool "FOTA relay transport interface (stub)"
	help
	  Exposes the fota_relay_transport_api vtable header for transports
	  that relay firmware updates from the gateway to sensor nodes.
	  No implementation is provided; this is a placeholder for future
	  wireless relay support (LoRa, BLE). See ADR-014.
```

- [ ] **Step 5: Register in root `CMakeLists.txt`**

Add after the last `add_subdirectory_ifdef` line in `CMakeLists.txt`:

```cmake
add_subdirectory_ifdef(CONFIG_FOTA_RELAY lib/fota_relay)
add_subdirectory_ifdef(CONFIG_FOTA_CONFIRM lib/fota_confirm)
```

- [ ] **Step 6: Register in `lib/Kconfig`**

Add at the end of `lib/Kconfig`:

```kconfig
rsource "fota_relay/Kconfig"
rsource "fota_confirm/Kconfig"
```

- [ ] **Step 7: Verify native_sim build still passes**

```bash
/build-and-test
```

Expected: tests pass (no code compiled yet — CONFIG_FOTA_RELAY is off).

- [ ] **Step 8: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add lib/fota_relay/ CMakeLists.txt lib/Kconfig
git commit -m "feat(fota): add fota_relay stub vtable header and lib scaffold"
```

---

## Task 5: HTTP FOTA Kconfig, handlers, and resource registration

**Files:**
- Modify: `lib/http_dashboard/Kconfig`
- Create: `lib/http_dashboard/src/fota.h`
- Create: `lib/http_dashboard/src/fota.c`
- Modify: `lib/http_dashboard/CMakeLists.txt`
- Modify: `lib/http_dashboard/src/http_dashboard.c`

- [ ] **Step 1: Add `CONFIG_HTTP_DASHBOARD_FOTA` to `lib/http_dashboard/Kconfig`**

Add before the closing `endif # HTTP_DASHBOARD` line:

```kconfig
config HTTP_DASHBOARD_FOTA
	bool "FOTA firmware upload endpoints (/api/fota/*)"
	depends on HTTP_DASHBOARD && HTTP_DASHBOARD_AUTH
	depends on BOOTLOADER_MCUBOOT && IMG_MANAGER && STREAM_FLASH && FLASH_MAP
	help
	  Adds three auth-gated endpoints to the HTTP dashboard:
	    POST /api/fota/upload  — stream signed image to flash slot 1
	    POST /api/fota/apply   — mark slot 1 pending and schedule reboot
	    GET  /api/fota/status  — return slot 0/1 versions and swap state
	  Requires HTTP_DASHBOARD_AUTH — upload is always authenticated.
```

- [ ] **Step 2: Create `lib/http_dashboard/src/fota.h`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <zephyr/net/http/server.h>

/**
 * Initialise the FOTA subsystem (registers the delayed reboot work item).
 * Called from http_dashboard_init().
 */
void fota_init(void);

int fota_upload_handler(struct http_client_ctx *client,
			enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data);

int fota_apply_handler(struct http_client_ctx *client,
		       enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx,
		       void *user_data);

int fota_status_handler(struct http_client_ctx *client,
			enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data);
```

- [ ] **Step 3: Create `lib/http_dashboard/src/fota.c`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/net/http/server.h>

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

int fota_upload_handler(struct http_client_ctx *client,
			enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data)
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
				static const char busy[] =
					"{\"error\":\"upload in progress\"}";
				response_ctx->status = HTTP_503_SERVICE_UNAVAILABLE;
				response_ctx->headers = json_ct_hdr;
				response_ctx->header_count = ARRAY_SIZE(json_ct_hdr);
				response_ctx->body = (const uint8_t *)busy;
				response_ctx->body_len = sizeof(busy) - 1;
				response_ctx->final_chunk = true;
			}
			return 0;
		}
		int rc = flash_img_init_id(&fota_img_ctx,
					   FIXED_PARTITION_ID(slot1_partition));
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
		int rc = flash_img_buffered_write(&fota_img_ctx,
						  request_ctx->data,
						  request_ctx->data_len,
						  last);
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

		snprintf((char *)ok_buf, sizeof(ok_buf),
			 "{\"ok\":true,\"bytes\":%zu}", written);
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

int fota_apply_handler(struct http_client_ctx *client,
		       enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx,
		       void *user_data)
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

int fota_status_handler(struct http_client_ctx *client,
			enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx,
			void *user_data)
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

	boot_read_bank_header(FIXED_PARTITION_ID(slot0_partition),
			      &hdr0, sizeof(hdr0));
	boot_read_bank_header(FIXED_PARTITION_ID(slot1_partition),
			      &hdr1, sizeof(hdr1));

	bool confirmed = boot_is_img_confirmed();
	int swap = mcuboot_swap_type();
	bool pending = (swap == BOOT_SWAP_TYPE_TEST ||
			swap == BOOT_SWAP_TYPE_REVERT);

	static uint8_t status_buf[256];
	int len = snprintf(
		(char *)status_buf, sizeof(status_buf),
		"{"
		"\"slot0\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":%s},"
		"\"slot1\":{\"version\":\"%u.%u.%u+%u\",\"confirmed\":false},"
		"\"pending\":%s"
		"}",
		hdr0.h.v1.sem_ver.major, hdr0.h.v1.sem_ver.minor,
		hdr0.h.v1.sem_ver.revision,
		(unsigned)hdr0.h.v1.sem_ver.build_num,
		confirmed ? "true" : "false",
		hdr1.h.v1.sem_ver.major, hdr1.h.v1.sem_ver.minor,
		hdr1.h.v1.sem_ver.revision,
		(unsigned)hdr1.h.v1.sem_ver.build_num,
		pending ? "true" : "false");

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
```

- [ ] **Step 4: Add `fota.c` to `lib/http_dashboard/CMakeLists.txt`**

After the `if(CONFIG_HTTP_DASHBOARD_AUTH)` block that adds `auth.c`, add:

```cmake
  if(CONFIG_HTTP_DASHBOARD_FOTA)
    zephyr_library_sources(src/fota.c)
  endif()
```

- [ ] **Step 5: Register FOTA resources in `lib/http_dashboard/src/http_dashboard.c`**

Add the `fota.h` include after the other conditional includes (near line 44):

```c
#if defined(CONFIG_HTTP_DASHBOARD_FOTA)
#	include "fota.h"
#endif
```

Add three resource detail structs and their `HTTP_RESOURCE_DEFINE` calls after the `api_locations_detail` block (before `HTTP_SERVICE_DEFINE`):

```c
#if defined(CONFIG_HTTP_DASHBOARD_FOTA)
static struct http_resource_detail_dynamic fota_upload_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = fota_upload_handler,
};
static struct http_resource_detail_dynamic fota_apply_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = fota_apply_handler,
};
static struct http_resource_detail_dynamic fota_status_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = fota_status_handler,
};
#endif /* CONFIG_HTTP_DASHBOARD_FOTA */
```

Add `HTTP_RESOURCE_DEFINE` calls after `api_locations_resource`:

```c
#if defined(CONFIG_HTTP_DASHBOARD_FOTA)
HTTP_RESOURCE_DEFINE(fota_upload_resource, dashboard_svc,
		     "/api/fota/upload", &fota_upload_detail);
HTTP_RESOURCE_DEFINE(fota_apply_resource, dashboard_svc,
		     "/api/fota/apply", &fota_apply_detail);
HTTP_RESOURCE_DEFINE(fota_status_resource, dashboard_svc,
		     "/api/fota/status", &fota_status_detail);
#endif
```

Add `fota_init()` call in `http_dashboard_init()` before `zbus_chan_add_obs`:

```c
#if defined(CONFIG_HTTP_DASHBOARD_FOTA)
	fota_init();
#endif
```

- [ ] **Step 6: Verify native_sim build passes**

```bash
/build-and-test
```

Expected: all tests pass. `CONFIG_HTTP_DASHBOARD_FOTA` is `n` on native_sim (no `BOOTLOADER_MCUBOOT`), so `fota.c` is not compiled.

- [ ] **Step 7: Verify hardware build compiles**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
west build apps/gateway \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  -d build/frdm_mcuboot_check
```

Expected: compiles without error. (Note: `CONFIG_HTTP_DASHBOARD_FOTA` is not yet enabled in the hardware conf — will be enabled in Task 7.)

- [ ] **Step 8: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add lib/http_dashboard/Kconfig \
        lib/http_dashboard/src/fota.h \
        lib/http_dashboard/src/fota.c \
        lib/http_dashboard/CMakeLists.txt \
        lib/http_dashboard/src/http_dashboard.c
git commit -m "feat(fota): add HTTP FOTA upload/apply/status endpoints to http_dashboard"
```

---

## Task 6: Image confirmation library

**Files:**
- Create: `lib/fota_confirm/Kconfig`
- Create: `lib/fota_confirm/CMakeLists.txt`
- Create: `lib/fota_confirm/src/fota_confirm.c`

- [ ] **Step 1: Create directory**

```bash
mkdir -p /home/zephyr/workspace/weather-station/lib/fota_confirm/src
```

- [ ] **Step 2: Create `lib/fota_confirm/Kconfig`**

```kconfig
# SPDX-License-Identifier: Apache-2.0

menuconfig FOTA_CONFIRM
	bool "Auto-confirm firmware image after successful boot"
	depends on BOOTLOADER_MCUBOOT
	help
	  Schedules boot_write_img_confirmed() after a configurable settle
	  delay at SYS_INIT APPLICATION priority 99 (after all other libs
	  have initialised). Without confirmation, MCUboot treats the running
	  image as a test image and reverts to the previous slot on the next
	  reset.

if FOTA_CONFIRM

config FOTA_CONFIRM_DELAY_S
	int "Settle delay before confirming image (seconds)"
	default 5
	range 1 60
	help
	  How long to wait after SYS_INIT APPLICATION 99 before calling
	  boot_write_img_confirmed(). Allows HTTP dashboard, MQTT, and SNTP
	  to bind their sockets before the image is marked permanent.

module = FOTA_CONFIRM
module-str = FOTA_CONFIRM
source "subsys/logging/Kconfig.template.log_config"

endif # FOTA_CONFIRM
```

- [ ] **Step 3: Create `lib/fota_confirm/CMakeLists.txt`**

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_FOTA_CONFIRM)
  zephyr_library()
  zephyr_library_sources(src/fota_confirm.c)
endif()
```

- [ ] **Step 4: Create `lib/fota_confirm/src/fota_confirm.c`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>

LOG_MODULE_REGISTER(fota_confirm, CONFIG_FOTA_CONFIRM_LOG_LEVEL);

static void confirm_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (boot_is_img_confirmed()) {
		return;
	}

	int rc = boot_write_img_confirmed();

	if (rc == 0) {
		LOG_INF("firmware update confirmed");
	} else {
		LOG_ERR("boot_write_img_confirmed failed: %d", rc);
	}
}

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_work_fn);

static int fota_confirm_init(void)
{
	k_work_schedule(&confirm_work, K_SECONDS(CONFIG_FOTA_CONFIRM_DELAY_S));
	return 0;
}

SYS_INIT(fota_confirm_init, APPLICATION, 99);
```

- [ ] **Step 5: Verify native_sim build passes**

```bash
/build-and-test
```

Expected: tests pass. `CONFIG_FOTA_CONFIRM` requires `BOOTLOADER_MCUBOOT`, which is absent on native_sim.

- [ ] **Step 6: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add lib/fota_confirm/
git commit -m "feat(fota): add fota_confirm library — auto-confirm after 5 s settle"
```

---

## Task 7: Enable FOTA on hardware and final build verification

**Files:**
- Modify: `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf`

- [ ] **Step 1: Enable FOTA and confirm libs in hardware board conf**

Add to the end of `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf`:

```ini
# ---------------------------------------------------------------
# FOTA HTTP endpoints + auto-confirm
# ---------------------------------------------------------------
CONFIG_HTTP_DASHBOARD_FOTA=y
CONFIG_FOTA_CONFIRM=y
CONFIG_FOTA_CONFIRM_DELAY_S=5
CONFIG_FOTA_RELAY=n
```

- [ ] **Step 2: Verify native_sim build is still clean**

```bash
/build-and-test
```

Expected: all integration tests pass, no regressions.

- [ ] **Step 3: Full hardware sysbuild — generate FOTA-capable artifacts**

```bash
cd /home/zephyr/workspace/weather-station
./scripts/gen-dev-key.sh

ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
west build apps/gateway \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  -d build/frdm_fota \
  "-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=$(pwd)/keys/dev-ed25519.pem"
```

Expected: build succeeds. Verify artifacts exist:

```bash
ls -lh build/frdm_fota/mcuboot/zephyr/zephyr.hex
ls -lh build/frdm_fota/gateway/zephyr/zephyr.signed.bin
```

Both files must be present. `zephyr.signed.bin` is the FOTA payload; it must be smaller than 984 KB (the slot size).

```bash
wc -c build/frdm_fota/gateway/zephyr/zephyr.signed.bin
```

Expected: less than `1007616` bytes (984 × 1024).

- [ ] **Step 4: Commit**

```bash
cd /home/zephyr/workspace/weather-station
git add apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf
git commit -m "feat(fota): enable HTTP_DASHBOARD_FOTA and FOTA_CONFIRM on frdm_mcxn947"
```

- [ ] **Step 5: Manual smoke test checklist (hardware only)**

When hardware is available, perform these steps in order:

```
1. Flash MCUboot once:
   west flash --runner jlink \
     --hex-file build/frdm_fota/mcuboot/zephyr/zephyr.hex

2. Flash initial application:
   west flash --runner jlink \
     --hex-file build/frdm_fota/merged.hex   (or use zephyr.signed.bin via mcumgr)

3. Verify auto-confirm fires (watch serial console):
   Expected log at ~5 s after boot:
     [fota_confirm] firmware update confirmed

4. Query status via HTTP:
   curl -s -H "Authorization: Bearer <token>" \
     http://<board-ip>:8080/api/fota/status | python3 -m json.tool
   Expected: slot0.confirmed = true, pending = false

5. Upload new image via HTTP:
   curl -s -X POST \
     -H "Authorization: Bearer <token>" \
     -H "Content-Type: application/octet-stream" \
     --data-binary @build/frdm_fota/gateway/zephyr.signed.bin \
     http://<board-ip>:8080/api/fota/upload
   Expected: {"ok":true,"bytes":<N>}

6. Apply update:
   curl -s -X POST \
     -H "Authorization: Bearer <token>" \
     http://<board-ip>:8080/api/fota/apply
   Expected: {"ok":true,"rebooting":true}
   Board reboots, MCUboot swaps slots, new image auto-confirms.

7. Verify rollback: flash a deliberately bad image
   (one that crashes before 5 s), observe MCUboot reverts to previous slot.

8. Test UART path:
   mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 \
     image upload build/frdm_fota/gateway/zephyr.signed.bin
   mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 \
     image test <hash-from-upload-output>
   mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 reset
```

---

## Self-review notes

- **native_sim isolation:** All FOTA Kconfig (`HTTP_DASHBOARD_FOTA`, `FOTA_CONFIRM`) depend on `BOOTLOADER_MCUBOOT`, which is never set in `prj.conf`. Safe.
- **No heap:** `flash_img_context` is a static local in `fota.c`. `fota_reboot_work` and `confirm_work` are static delayables. All buffers (`ok_buf`, `status_buf`) are `static uint8_t[]`.
- **Concurrent upload protection:** `atomic_cas(&fota_in_progress, 0, 1)` ensures only one upload proceeds at a time; the second caller gets 503.
- **Auth always required:** `CONFIG_HTTP_DASHBOARD_FOTA` depends on `HTTP_DASHBOARD_AUTH` — `auth_api_check()` is always available when `fota.c` is compiled.
- **`slot0_partition` / `slot1_partition`:** These DTS node names are defined in `frdm_mcxn947_mcxn947_cpu0.dtsi`. `FIXED_PARTITION_ID()` resolves them at compile time via `<zephyr/storage/flash_map.h>`.
- **`BOOT_SWAP_TYPE_TEST` / `BOOT_SWAP_TYPE_REVERT`:** Defined in `<zephyr/dfu/mcuboot.h>`. Used in `fota_status_handler` to determine `pending`.
