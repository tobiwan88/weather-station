/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file remote_sensor_settings.c
 * @brief Persistent storage for known remote sensor peers.
 *
 * Settings key layout (prefix "rsen/"):
 *   rsen/<uid_hex>/proto     uint8_t  enum remote_transport_proto
 *   rsen/<uid_hex>/addr      bytes    peer_addr (variable length)
 *   rsen/<uid_hex>/addr_len  uint8_t
 *   rsen/<uid_hex>/label     string   display label at discovery time
 *   rsen/<uid_hex>/type      uint8_t  enum sensor_type
 *
 * On boot (SYS_INIT APPLICATION 94):
 *   - loads all "rsen/" subtrees via settings_load_subtree()
 *   - calls remote_sensor_manager_restore() for each complete record
 *   - manager re-registers the peer in sensor_registry and calls peer_add()
 *
 * Consumers see remote sensors in the registry before scanning begins.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/iterable_sections.h>

#include "remote_sensor_priv.h"
#include <remote_sensor/remote_sensor.h>

LOG_MODULE_DECLARE(remote_sensor, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * Per-peer load context — accumulates fields for one uid during load
 * -------------------------------------------------------------------------- */

struct peer_load_ctx {
	uint32_t uid;
	enum remote_transport_proto proto;
	uint8_t peer_addr[REMOTE_SENSOR_ADDR_MAX_LEN];
	uint8_t peer_addr_len;
	char label[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];
	enum sensor_type sensor_type;
	uint8_t flags; /* bitmask of RSEN_FLAG_* */
};

#define RSEN_FLAG_PROTO    BIT(0)
#define RSEN_FLAG_ADDR     BIT(1)
#define RSEN_FLAG_ADDR_LEN BIT(2)
#define RSEN_FLAG_LABEL    BIT(3)
#define RSEN_FLAG_TYPE     BIT(4)
#define RSEN_FLAG_COMPLETE                                                                          \
	(RSEN_FLAG_PROTO | RSEN_FLAG_ADDR | RSEN_FLAG_ADDR_LEN | RSEN_FLAG_LABEL | RSEN_FLAG_TYPE)

static struct peer_load_ctx load_ctx[CONFIG_REMOTE_SENSOR_MAX_PEERS];
static int load_ctx_count;

static struct peer_load_ctx *get_or_alloc_ctx(uint32_t uid)
{
	for (int i = 0; i < load_ctx_count; i++) {
		if (load_ctx[i].uid == uid) {
			return &load_ctx[i];
		}
	}
	if (load_ctx_count >= CONFIG_REMOTE_SENSOR_MAX_PEERS) {
		return NULL;
	}
	struct peer_load_ctx *ctx = &load_ctx[load_ctx_count++];

	memset(ctx, 0, sizeof(*ctx));
	ctx->uid = uid;
	return ctx;
}

/* --------------------------------------------------------------------------
 * settings handler — called for each "rsen/<uid>/<field>" key
 * -------------------------------------------------------------------------- */

static int rsen_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);

	const char *slash = strchr(key, '/');

	if (!slash) {
		return -EINVAL;
	}

	size_t uid_len = (size_t)(slash - key);
	char uid_str[9];

	if (uid_len >= sizeof(uid_str)) {
		return -EINVAL;
	}
	memcpy(uid_str, key, uid_len);
	uid_str[uid_len] = '\0';

	uint32_t uid = (uint32_t)strtoul(uid_str, NULL, 16);
	const char *field = slash + 1;

	struct peer_load_ctx *ctx = get_or_alloc_ctx(uid);

	if (!ctx) {
		LOG_WRN("rsen: peer table full, skipping uid 0x%08x", uid);
		return -ENOMEM;
	}

	if (strcmp(field, "proto") == 0) {
		uint8_t v;

		if (read_cb(cb_arg, &v, sizeof(v)) == sizeof(v)) {
			ctx->proto = (enum remote_transport_proto)v;
			ctx->flags |= RSEN_FLAG_PROTO;
		}
	} else if (strcmp(field, "addr") == 0) {
		ssize_t n = read_cb(cb_arg, ctx->peer_addr, sizeof(ctx->peer_addr));
		if (n > 0) {
			ctx->flags |= RSEN_FLAG_ADDR;
		}
	} else if (strcmp(field, "addr_len") == 0) {
		uint8_t v;

		if (read_cb(cb_arg, &v, sizeof(v)) == sizeof(v)) {
			ctx->peer_addr_len = v;
			ctx->flags |= RSEN_FLAG_ADDR_LEN;
		}
	} else if (strcmp(field, "label") == 0) {
		ssize_t n = read_cb(cb_arg, ctx->label, sizeof(ctx->label) - 1);
		if (n >= 0) {
			ctx->label[n] = '\0';
			ctx->flags |= RSEN_FLAG_LABEL;
		}
	} else if (strcmp(field, "type") == 0) {
		uint8_t v;

		if (read_cb(cb_arg, &v, sizeof(v)) == sizeof(v)) {
			ctx->sensor_type = (enum sensor_type)v;
			ctx->flags |= RSEN_FLAG_TYPE;
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(rsen, "rsen", NULL, rsen_set, NULL, NULL);

/* --------------------------------------------------------------------------
 * Commit — restore complete peers into sensor_registry via the manager
 * -------------------------------------------------------------------------- */

static void rsen_commit(void)
{
	for (int i = 0; i < load_ctx_count; i++) {
		struct peer_load_ctx *ctx = &load_ctx[i];

		if ((ctx->flags & RSEN_FLAG_COMPLETE) != RSEN_FLAG_COMPLETE) {
			LOG_WRN("rsen: incomplete record uid 0x%08x (flags=0x%x), skipping",
				ctx->uid, ctx->flags);
			continue;
		}

		int rc = remote_sensor_manager_restore(ctx->uid, ctx->proto, ctx->peer_addr,
						       ctx->peer_addr_len, ctx->label,
						       ctx->sensor_type);

		if (rc != 0) {
			LOG_ERR("rsen: restore uid 0x%08x failed: %d", ctx->uid, rc);
		}
	}
}

/* --------------------------------------------------------------------------
 * Save helper — called by remote_sensor_manager after a new registration
 * -------------------------------------------------------------------------- */

void remote_sensor_settings_save(const struct remote_peer *peer, const char *label,
				 enum sensor_type type)
{
	char key[40];
	uint8_t proto_byte = (uint8_t)peer->proto;
	uint8_t type_byte = (uint8_t)type;

	snprintf(key, sizeof(key), "rsen/%08x/proto", peer->uid);
	settings_save_one(key, &proto_byte, sizeof(proto_byte));

	snprintf(key, sizeof(key), "rsen/%08x/addr", peer->uid);
	settings_save_one(key, peer->peer_addr, peer->peer_addr_len);

	snprintf(key, sizeof(key), "rsen/%08x/addr_len", peer->uid);
	settings_save_one(key, &peer->peer_addr_len, sizeof(peer->peer_addr_len));

	snprintf(key, sizeof(key), "rsen/%08x/label", peer->uid);
	settings_save_one(key, label, strlen(label));

	snprintf(key, sizeof(key), "rsen/%08x/type", peer->uid);
	settings_save_one(key, &type_byte, sizeof(type_byte));

	LOG_DBG("rsen: saved uid=0x%08x", peer->uid);
}

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 94 — load after manager (92) and transports (93)
 * -------------------------------------------------------------------------- */

static int remote_sensor_settings_init(void)
{
	int rc = settings_load_subtree("rsen");

	if (rc != 0) {
		LOG_WRN("settings_load_subtree(rsen) failed: %d", rc);
	}
	rsen_commit();
	return 0;
}

SYS_INIT(remote_sensor_settings_init, APPLICATION, 94);
