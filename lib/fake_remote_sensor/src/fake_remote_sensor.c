/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_remote_sensor.c
 * @brief Fake remote sensor transport — simulates wireless nodes on native_sim.
 *
 * Each "node" is identified by a 1-byte node_id (0, 1, 2, …) and a fixed
 * 6-byte fake MAC address derived from the node_id.  The transport:
 *
 *   1. Registers itself via REMOTE_TRANSPORT_DEFINE (STRUCT_SECTION_ITERABLE).
 *   2. On scan_start: publishes REMOTE_DISCOVERY_FOUND events for all nodes.
 *   3. On peer_add: starts a k_timer for that peer's auto-publish interval.
 *   4. On send_trigger: immediately publishes one synthetic measurement.
 *   5. Shell "fake_remote announce" — manually trigger discovery.
 *   6. Shell "fake_remote publish" — manually trigger one measurement round.
 *
 * Synthetic values:
 *   temperature: 20.0 °C + node_id (to distinguish nodes)
 *   humidity:    50.0 %RH + node_id * 5
 *   co2:         800 ppm + node_id * 100
 *   voc:         25 IAQ  + node_id * 50
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>

#include <fake_remote_sensor/fake_remote_sensor.h>
#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(fake_remote_sensor, CONFIG_FAKE_REMOTE_SENSOR_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Per-node state
 * -------------------------------------------------------------------------- */

struct fake_node {
	uint8_t node_id;
	uint8_t mac[6]; /* fake MAC: {0xFA, 0x4E, 0x00, 0x00, 0x00, node_id} */
	uint32_t uid_temp;
	uint32_t uid_hum;
	uint32_t uid_co2;
	uint32_t uid_voc;
	bool registered;
	struct k_timer auto_timer;
};

static struct fake_node nodes[CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT];

/* --------------------------------------------------------------------------
 * Synthetic value generation
 * -------------------------------------------------------------------------- */

static void publish_node(struct fake_node *n)
{
	/* Temperature: 20.0 + node_id °C */
	double temp_c = 20.0 + (double)n->node_id;
	int rc = remote_sensor_publish_data(n->uid_temp, SENSOR_TYPE_TEMPERATURE,
					    temperature_c_to_q31(temp_c));
	if (rc != 0) {
		LOG_WRN("node %u: temp pub failed (%d)", n->node_id, rc);
	}

	/* Humidity: 50.0 + node_id * 5 %RH */
	double hum_pct = 50.0 + (double)n->node_id * 5.0;
	rc = remote_sensor_publish_data(n->uid_hum, SENSOR_TYPE_HUMIDITY,
					humidity_pct_to_q31(hum_pct));
	if (rc != 0) {
		LOG_WRN("node %u: hum pub failed (%d)", n->node_id, rc);
	}

	/* CO₂: 800 + node_id * 100 ppm */
	double co2_ppm = 800.0 + (double)n->node_id * 100.0;
	rc = remote_sensor_publish_data(n->uid_co2, SENSOR_TYPE_CO2, co2_ppm_to_q31(co2_ppm));
	if (rc != 0) {
		LOG_WRN("node %u: co2 pub failed (%d)", n->node_id, rc);
	}

	/* VOC: 25 + node_id * 50 IAQ */
	double voc_iaq = 25.0 + (double)n->node_id * 50.0;
	rc = remote_sensor_publish_data(n->uid_voc, SENSOR_TYPE_VOC, voc_iaq_to_q31(voc_iaq));
	if (rc != 0) {
		LOG_WRN("node %u: voc pub failed (%d)", n->node_id, rc);
	}

	LOG_DBG("node %u: published temp=%.1f hum=%.1f co2=%.0f voc=%.1f", n->node_id,
		(double)temp_c, (double)hum_pct, (double)co2_ppm, (double)voc_iaq);
}

static void auto_timer_cb(struct k_timer *timer)
{
	struct fake_node *n = CONTAINER_OF(timer, struct fake_node, auto_timer);

	if (n->registered) {
		publish_node(n);
	}
}

/* --------------------------------------------------------------------------
 * Discovery announcements
 * -------------------------------------------------------------------------- */

static int announce_node(struct fake_node *n)
{
	struct remote_discovery_event evt = {
		.action = REMOTE_DISCOVERY_FOUND,
		.proto = REMOTE_TRANSPORT_PROTO_FAKE,
		.peer_addr_len = sizeof(n->mac),
	};
	memcpy(evt.peer_addr, n->mac, sizeof(n->mac));

	/* Temperature discovery */
	snprintf(evt.suggested_label, sizeof(evt.suggested_label), "fake-remote-%u-temp",
		 n->node_id);
	evt.sensor_type = SENSOR_TYPE_TEMPERATURE;
	evt.suggested_uid = n->uid_temp;

	int rc = remote_sensor_announce_disc(&evt);

	if (rc != 0) {
		LOG_ERR("node %u: discovery enqueue (temp) failed: %d", n->node_id, rc);
		return rc;
	}

	/* Allow workqueue to process before next announcement. */
	k_sleep(K_MSEC(10));

	/* Humidity discovery — different uid, same address, different type */
	snprintf(evt.suggested_label, sizeof(evt.suggested_label), "fake-remote-%u-hum",
		 n->node_id);
	evt.sensor_type = SENSOR_TYPE_HUMIDITY;
	evt.suggested_uid = n->uid_hum;

	rc = remote_sensor_announce_disc(&evt);
	if (rc != 0) {
		LOG_ERR("node %u: discovery enqueue (hum) failed: %d", n->node_id, rc);
		return rc;
	}

	k_sleep(K_MSEC(10));

	/* CO₂ discovery */
	snprintf(evt.suggested_label, sizeof(evt.suggested_label), "fake-remote-%u-co2",
		 n->node_id);
	evt.sensor_type = SENSOR_TYPE_CO2;
	evt.suggested_uid = n->uid_co2;

	rc = remote_sensor_announce_disc(&evt);
	if (rc != 0) {
		LOG_ERR("node %u: discovery enqueue (co2) failed: %d", n->node_id, rc);
		return rc;
	}

	k_sleep(K_MSEC(10));

	/* VOC discovery */
	snprintf(evt.suggested_label, sizeof(evt.suggested_label), "fake-remote-%u-voc",
		 n->node_id);
	evt.sensor_type = SENSOR_TYPE_VOC;
	evt.suggested_uid = n->uid_voc;

	rc = remote_sensor_announce_disc(&evt);
	if (rc != 0) {
		LOG_ERR("node %u: discovery enqueue (voc) failed: %d", n->node_id, rc);
		return rc;
	}

	LOG_INF("announced fake node %u (uid_temp=0x%08x uid_hum=0x%08x uid_co2=0x%08x "
		"uid_voc=0x%08x)",
		n->node_id, n->uid_temp, n->uid_hum, n->uid_co2, n->uid_voc);
	return 0;
}

/* --------------------------------------------------------------------------
 * Transport vtable implementation
 * -------------------------------------------------------------------------- */

static int fake_scan_start(const struct remote_transport *t)
{
	ARG_UNUSED(t);
	LOG_INF("fake scan start: announcing %d node(s)", CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT);
	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		announce_node(&nodes[i]);
	}
	return 0;
}

static int fake_scan_stop(const struct remote_transport *t)
{
	ARG_UNUSED(t);
	LOG_INF("fake scan stop (no-op for stub)");
	return 0;
}

static int fake_peer_add(const struct remote_transport *t, const uint8_t *peer_addr,
			 size_t addr_len, uint32_t uid)
{
	ARG_UNUSED(t);
	ARG_UNUSED(peer_addr);
	ARG_UNUSED(addr_len);

	/* Find the node that owns this uid and mark it registered. */
	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		struct fake_node *n = &nodes[i];

		if (n->uid_temp == uid || n->uid_hum == uid || n->uid_co2 == uid ||
		    n->uid_voc == uid) {
			if (!n->registered) {
				n->registered = true;
				LOG_INF("peer_add: node %u registered (uid=0x%08x)", n->node_id,
					uid);
#if CONFIG_FAKE_REMOTE_SENSOR_AUTO_PUBLISH_MS > 0
				k_timer_start(&n->auto_timer,
					      K_MSEC(CONFIG_FAKE_REMOTE_SENSOR_AUTO_PUBLISH_MS),
					      K_MSEC(CONFIG_FAKE_REMOTE_SENSOR_AUTO_PUBLISH_MS));
#endif
			}
			return 0;
		}
	}
	LOG_WRN("peer_add: uid 0x%08x not found in fake nodes", uid);
	return -ENOENT;
}

static int fake_peer_remove(const struct remote_transport *t, uint32_t uid)
{
	ARG_UNUSED(t);

	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		struct fake_node *n = &nodes[i];

		if (n->uid_temp == uid || n->uid_hum == uid || n->uid_co2 == uid ||
		    n->uid_voc == uid) {
			n->registered = false;
			k_timer_stop(&n->auto_timer);
			LOG_INF("peer_remove: node %u removed (uid=0x%08x)", n->node_id, uid);
			return 0;
		}
	}
	return -ENOENT;
}

static int fake_send_trigger(const struct remote_transport *t, uint32_t uid)
{
	ARG_UNUSED(t);

	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		struct fake_node *n = &nodes[i];

		if (n->uid_temp == uid || n->uid_hum == uid || n->uid_co2 == uid ||
		    n->uid_voc == uid) {
			publish_node(n);
			return 0;
		}
	}
	return -ENOENT;
}

REMOTE_TRANSPORT_DEFINE(fake_remote_transport,
			{
				.name = "fake",
				.proto = REMOTE_TRANSPORT_PROTO_FAKE,
				.caps = REMOTE_TRANSPORT_CAP_SCAN | REMOTE_TRANSPORT_CAP_TRIGGER,
				.scan_start = fake_scan_start,
				.scan_stop = fake_scan_stop,
				.peer_add = fake_peer_add,
				.peer_remove = fake_peer_remove,
				.send_trigger = fake_send_trigger,
			});

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int fake_remote_sensor_announce(void)
{
	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		int rc = announce_node(&nodes[i]);

		if (rc != 0) {
			return rc;
		}
	}
	return 0;
}

int fake_remote_sensor_publish_all(void)
{
	int first_err = 0;

	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		if (!nodes[i].registered) {
			continue;
		}
		publish_node(&nodes[i]);
	}
	return first_err;
}

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 93 — init node state + timers
 * -------------------------------------------------------------------------- */

static int fake_remote_sensor_init(void)
{
	for (int i = 0; i < CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT; i++) {
		struct fake_node *n = &nodes[i];

		n->node_id = (uint8_t)i;
		n->mac[0] = 0xFA;
		n->mac[1] = 0x4E; /* "FAKE" */
		n->mac[2] = 0x00;
		n->mac[3] = 0x00;
		n->mac[4] = 0x00;
		n->mac[5] = (uint8_t)i;

		n->uid_temp =
			remote_sensor_uid_from_addr(CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX, n->mac,
						    sizeof(n->mac), SENSOR_TYPE_TEMPERATURE);

		n->uid_hum =
			remote_sensor_uid_from_addr(CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX, n->mac,
						    sizeof(n->mac), SENSOR_TYPE_HUMIDITY);

		n->uid_co2 = remote_sensor_uid_from_addr(CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX,
							 n->mac, sizeof(n->mac), SENSOR_TYPE_CO2);

		n->uid_voc = remote_sensor_uid_from_addr(CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX,
							 n->mac, sizeof(n->mac), SENSOR_TYPE_VOC);

		n->registered = false;
		k_timer_init(&n->auto_timer, auto_timer_cb, NULL);
	}

	LOG_INF("fake_remote_sensor: init done (%d node(s))", CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT);
	return 0;
}

SYS_INIT(fake_remote_sensor_init, APPLICATION, 93);
