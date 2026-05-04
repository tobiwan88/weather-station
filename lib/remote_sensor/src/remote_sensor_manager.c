/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file remote_sensor_manager.c
 * @brief Remote sensor manager: listener+workqueue dispatch.
 *
 * Responsibilities:
 *   - On boot: subscribe to channels, optionally auto-scan all transports.
 *   - Processes remote_scan_ctrl_events → dispatches scan_start/stop to
 *     matching transports via STRUCT_SECTION_FOREACH.
 *   - Processes remote_discovery_events → registers new sensors in
 *     sensor_registry, calls peer_add on the matching transport, persists.
 *   - Listens to sensor_trigger_chan (fast LISTENER path) → forwards triggers
 *     to capable transports.
 *
 * Design: zbus listeners dispatch to work items on a dedicated workqueue.
 * No dedicated thread — the workqueue handles all deferred processing.
 */

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include "remote_sensor_priv.h"
#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

LOG_MODULE_REGISTER(remote_sensor, CONFIG_REMOTE_SENSOR_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Peer table — owned by this module
 * -------------------------------------------------------------------------- */

static struct remote_peer peer_table[CONFIG_REMOTE_SENSOR_MAX_PEERS];

/* Stable label strings and registry entries pointed to by sensor_registry. */
static char peer_labels[CONFIG_REMOTE_SENSOR_MAX_PEERS][CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];
static struct sensor_registry_entry peer_reg_entries[CONFIG_REMOTE_SENSOR_MAX_PEERS];

K_MUTEX_DEFINE(peer_table_mutex);

/* --------------------------------------------------------------------------
 * Peer table helpers
 * -------------------------------------------------------------------------- */

static struct remote_peer *peer_alloc(void)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (!peer_table[i].used) {
			return &peer_table[i];
		}
	}
	return NULL;
}

#if defined(CONFIG_REMOTE_SENSOR_PERSIST)
static struct remote_peer *peer_find_by_uid(uint32_t uid)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (peer_table[i].used && peer_table[i].uid == uid) {
			return &peer_table[i];
		}
	}
	return NULL;
}
#endif /* CONFIG_REMOTE_SENSOR_PERSIST */

static int peer_slot_index(const struct remote_peer *peer)
{
	return (int)(peer - peer_table);
}

/* --------------------------------------------------------------------------
 * Transport lookup
 * -------------------------------------------------------------------------- */

static const struct remote_transport *find_transport(enum remote_transport_proto proto)
{
	STRUCT_SECTION_FOREACH(remote_transport, t)
	{
		if (t->proto == proto) {
			return t;
		}
	}
	return NULL;
}

/* --------------------------------------------------------------------------
 * UID helpers (public API, declared in remote_sensor.h)
 * -------------------------------------------------------------------------- */

/**
 * Simple 12-bit CRC for BLE/Thread UID derivation.
 * Not a standards-based polynomial — purpose is stable spreading of
 * hardware addresses into 4096 device slots per protocol prefix.
 */
static uint16_t crc12(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 4;
		for (int b = 0; b < 8; b++) {
			if (crc & 0x800) {
				crc = (uint16_t)((crc << 1) ^ 0x80F);
			} else {
				crc = (uint16_t)(crc << 1);
			}
			crc &= 0xFFF;
		}
	}
	return crc;
}

uint32_t remote_sensor_uid_from_addr(uint16_t prefix, const uint8_t *addr, size_t len,
				     enum sensor_type type)
{
	return ((uint32_t)prefix << 16) | ((uint32_t)crc12(addr, len) << 4) |
	       ((uint32_t)type & 0x0F);
}

uint32_t remote_sensor_uid_from_node_id(uint16_t prefix, uint8_t node_id, enum sensor_type type)
{
	return ((uint32_t)prefix << 16) | ((uint32_t)node_id << 4) | ((uint32_t)type & 0x0F);
}

/* --------------------------------------------------------------------------
 * remote_sensor_publish_data — public API for transport adapters
 * -------------------------------------------------------------------------- */

int remote_sensor_publish_data(uint32_t uid, enum sensor_type type, int32_t q31_value)
{
	struct env_sensor_data evt = {
		.sensor_uid = uid,
		.type = type,
		.q31_value = q31_value,
		.timestamp_ms = k_uptime_get(),
	};
	int rc = zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);

	if (rc != 0) {
		LOG_WRN("uid 0x%08x: pub failed (%d)", uid, rc);
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * Core registration logic — shared by discovery path and settings restore
 * -------------------------------------------------------------------------- */

static int register_peer(uint32_t uid, enum remote_transport_proto proto, const uint8_t *peer_addr,
			 uint8_t peer_addr_len, const char *label, enum sensor_type type)
{
	if (sensor_registry_lookup(uid) != NULL) {
		LOG_DBG("uid 0x%08x already registered, skipping", uid);
		return 0;
	}

	k_mutex_lock(&peer_table_mutex, K_FOREVER);

	struct remote_peer *peer = peer_alloc();

	if (!peer) {
		k_mutex_unlock(&peer_table_mutex);
		LOG_ERR("peer table full (max %d)", CONFIG_REMOTE_SENSOR_MAX_PEERS);
		return -ENOMEM;
	}

	int idx = peer_slot_index(peer);

	peer->uid = uid;
	peer->proto = proto;
	peer->peer_addr_len = (uint8_t)MIN(peer_addr_len, REMOTE_SENSOR_ADDR_MAX_LEN);
	memcpy(peer->peer_addr, peer_addr, peer->peer_addr_len);
	peer->used = true;

	strncpy(peer_labels[idx], label, sizeof(peer_labels[idx]) - 1);
	peer_labels[idx][sizeof(peer_labels[idx]) - 1] = '\0';

	peer_reg_entries[idx] = (struct sensor_registry_entry){
		.uid = uid,
		.label = peer_labels[idx],
		.is_remote = true,
	};

	k_mutex_unlock(&peer_table_mutex);

	int rc = sensor_registry_register(&peer_reg_entries[idx]);

	if (rc != 0 && rc != -EEXIST) {
		LOG_ERR("registry register uid 0x%08x failed: %d", uid, rc);
		k_mutex_lock(&peer_table_mutex, K_FOREVER);
		peer->used = false;
		k_mutex_unlock(&peer_table_mutex);
		return rc;
	}

	struct sensor_registry_meta meta = {0};

	strncpy(meta.display_name, label, sizeof(meta.display_name) - 1);
	meta.enabled = true;
	sensor_registry_set_meta(uid, &meta);

	LOG_INF("registered remote sensor uid=0x%08x label='%s' proto=%d type=%d", uid,
		peer_labels[idx], (int)proto, (int)type);

	const struct remote_transport *t = find_transport(proto);

	if (t && t->peer_add) {
		rc = t->peer_add(t, peer->peer_addr, peer->peer_addr_len, uid);
		if (rc != 0) {
			LOG_WRN("peer_add uid 0x%08x failed: %d", uid, rc);
		}
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * remote_sensor_manager_restore — called by settings on boot
 * -------------------------------------------------------------------------- */

int remote_sensor_manager_restore(uint32_t uid, enum remote_transport_proto proto,
				  const uint8_t *peer_addr, uint8_t peer_addr_len,
				  const char *label, enum sensor_type type)
{
	int rc = register_peer(uid, proto, peer_addr, peer_addr_len, label, type);

	if (rc == 0) {
		LOG_INF("rsen: restored uid=0x%08x '%s'", uid, label);
	}
	return rc;
}

/* --------------------------------------------------------------------------
 * Discovery event handler
 * -------------------------------------------------------------------------- */

static void handle_discovery(const struct remote_discovery_event *evt)
{
	if (evt->action == REMOTE_DISCOVERY_FOUND) {
#if defined(CONFIG_REMOTE_SENSOR_AUTO_REGISTER)
		int rc = register_peer(evt->suggested_uid, evt->proto, evt->peer_addr,
				       evt->peer_addr_len, evt->suggested_label, evt->sensor_type);

		if (rc != 0) {
			return;
		}

#	if defined(CONFIG_REMOTE_SENSOR_PERSIST)
		k_mutex_lock(&peer_table_mutex, K_FOREVER);
		struct remote_peer *peer = peer_find_by_uid(evt->suggested_uid);
		k_mutex_unlock(&peer_table_mutex);

		if (peer) {
			remote_sensor_settings_save(peer, evt->suggested_label, evt->sensor_type);
		}
#	endif /* CONFIG_REMOTE_SENSOR_PERSIST */
#else
		LOG_INF("discovered uid=0x%08x '%s' (proto=%d) — "
			"awaiting 'remote_sensor pair'",
			evt->suggested_uid, evt->suggested_label, (int)evt->proto);
#endif /* CONFIG_REMOTE_SENSOR_AUTO_REGISTER */
	} else {
		/* LOST: keep in registry, mark disabled. */
		LOG_INF("lost contact uid=0x%08x", evt->suggested_uid);
		struct sensor_registry_meta meta;

		if (sensor_registry_get_meta(evt->suggested_uid, &meta) == 0) {
			meta.enabled = false;
			sensor_registry_set_meta(evt->suggested_uid, &meta);
		}
	}
}

/* --------------------------------------------------------------------------
 * Discovery announce — thin wrapper publishing to zbus channel
 * -------------------------------------------------------------------------- */

int remote_sensor_announce_disc(const struct remote_discovery_event *evt)
{
	return zbus_chan_pub(&remote_discovery_chan, evt, K_MSEC(100));
}

/* --------------------------------------------------------------------------
 * Workqueue + work items for listener dispatch
 * -------------------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(remote_sensor_wq_stack, CONFIG_REMOTE_SENSOR_THREAD_STACK_SIZE);
static struct k_work_q remote_sensor_wq;

static struct k_work disc_work;
static struct k_work scan_work;
static struct remote_discovery_event pending_disc;
static struct remote_scan_ctrl_event pending_scan;
static K_MUTEX_DEFINE(disc_pending_mutex);
static K_MUTEX_DEFINE(scan_pending_mutex);

/* --------------------------------------------------------------------------
 * Discovery work handler + zbus listener
 * -------------------------------------------------------------------------- */

static void disc_work_handler(struct k_work *work)
{
	struct remote_discovery_event evt;

	ARG_UNUSED(work);

	k_mutex_lock(&disc_pending_mutex, K_FOREVER);
	evt = pending_disc;
	k_mutex_unlock(&disc_pending_mutex);
	handle_discovery(&evt);
}

static void discovery_cb(const struct zbus_channel *chan)
{
	const struct remote_discovery_event *evt = zbus_chan_const_msg(chan);

	k_mutex_lock(&disc_pending_mutex, K_FOREVER);
	pending_disc = *evt;
	k_mutex_unlock(&disc_pending_mutex);
	(void)k_work_submit_to_queue(&remote_sensor_wq, &disc_work);
}

ZBUS_LISTENER_DEFINE(remote_disc_listener, discovery_cb);

/* --------------------------------------------------------------------------
 * Scan control work handler + zbus listener
 * -------------------------------------------------------------------------- */

static void scan_work_handler(struct k_work *work)
{
	struct remote_scan_ctrl_event evt;

	ARG_UNUSED(work);

	k_mutex_lock(&scan_pending_mutex, K_FOREVER);
	evt = pending_scan;
	k_mutex_unlock(&scan_pending_mutex);

	STRUCT_SECTION_FOREACH(remote_transport, t)
	{
		bool match = evt.proto == REMOTE_TRANSPORT_PROTO_UNKNOWN || t->proto == evt.proto;
		bool can_scan = t->caps & REMOTE_TRANSPORT_CAP_SCAN;

		if (!match || !can_scan) {
			continue;
		}
		if (evt.action == REMOTE_SCAN_START) {
			LOG_INF("scan start: %s", t->name);
			if (t->scan_start) {
				t->scan_start(t);
			}
		} else {
			LOG_INF("scan stop: %s", t->name);
			if (t->scan_stop) {
				t->scan_stop(t);
			}
		}
	}
}

static void scan_ctrl_cb(const struct zbus_channel *chan)
{
	const struct remote_scan_ctrl_event *evt = zbus_chan_const_msg(chan);

	k_mutex_lock(&scan_pending_mutex, K_FOREVER);
	pending_scan = *evt;
	k_mutex_unlock(&scan_pending_mutex);
	(void)k_work_submit_to_queue(&remote_sensor_wq, &scan_work);
}

ZBUS_LISTENER_DEFINE(remote_scan_ctrl_listener, scan_ctrl_cb);

/* --------------------------------------------------------------------------
 * zbus LISTENER — trigger routing (fast, non-blocking)
 * -------------------------------------------------------------------------- */

static void remote_trigger_cb(const struct zbus_channel *chan)
{
	const struct sensor_trigger_event *trig = zbus_chan_const_msg(chan);

	k_mutex_lock(&peer_table_mutex, K_FOREVER);

	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (!peer_table[i].used) {
			continue;
		}
		if (trig->target_uid != 0 && trig->target_uid != peer_table[i].uid) {
			continue;
		}

		const struct remote_transport *t = find_transport(peer_table[i].proto);

		if (!t || !(t->caps & REMOTE_TRANSPORT_CAP_TRIGGER) || !t->send_trigger) {
			continue;
		}

		uint32_t uid = peer_table[i].uid;

		LOG_DBG("trigger → uid=0x%08x proto=%d", uid, peer_table[i].proto);
		k_mutex_unlock(&peer_table_mutex);
		t->send_trigger(t, uid);
		k_mutex_lock(&peer_table_mutex, K_FOREVER);
	}

	k_mutex_unlock(&peer_table_mutex);
}

ZBUS_LISTENER_DEFINE(remote_trigger_listener, remote_trigger_cb);

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 92 — init work items, kick off auto-scan
 * -------------------------------------------------------------------------- */

static int remote_sensor_manager_init(void)
{
	int rc;

	k_work_init(&disc_work, disc_work_handler);
	k_work_init(&scan_work, scan_work_handler);
	k_work_queue_start(&remote_sensor_wq, remote_sensor_wq_stack,
			   K_THREAD_STACK_SIZEOF(remote_sensor_wq_stack),
			   CONFIG_REMOTE_SENSOR_THREAD_PRIORITY, NULL);

	rc = zbus_chan_add_obs(&remote_discovery_chan, &remote_disc_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("add obs remote_discovery_chan: %d", rc);
		return rc;
	}

	rc = zbus_chan_add_obs(&remote_scan_ctrl_chan, &remote_scan_ctrl_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("add obs remote_scan_ctrl_chan: %d", rc);
		return rc;
	}

	rc = zbus_chan_add_obs(&sensor_trigger_chan, &remote_trigger_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("add obs sensor_trigger_chan: %d", rc);
		return rc;
	}

#if defined(CONFIG_REMOTE_SENSOR_AUTO_SCAN)
	struct remote_scan_ctrl_event scan_all = {
		.action = REMOTE_SCAN_START,
		.proto = REMOTE_TRANSPORT_PROTO_UNKNOWN,
	};
	zbus_chan_pub(&remote_scan_ctrl_chan, &scan_all, K_MSEC(100));
	LOG_INF("auto-scan started");
#endif

	LOG_INF("remote_sensor_manager: init done");
	return 0;
}

SYS_INIT(remote_sensor_manager_init, APPLICATION, 92);
