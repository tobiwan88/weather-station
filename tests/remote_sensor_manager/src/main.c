/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/remote_sensor_manager)
 * @brief Integration tests for the remote_sensor_manager.
 *
 * Uses fake_remote_sensor as the transport adapter — no real BLE/LoRa needed.
 *
 * CONFIG_FAKE_REMOTE_SENSOR_NODE_COUNT=1, AUTO_PUBLISH_MS=0, AUTO_SCAN=n so
 * all events are triggered manually from the test, giving deterministic timing.
 *
 * Test sequence:
 *   setup: subscribe test listener to sensor_event_chan
 *   t1: STRUCT_SECTION_FOREACH finds the fake transport
 *   t2: announce → manager registers sensors in sensor_registry
 *   t3: publish_all → env_sensor_data appears on sensor_event_chan
 *   t4: sensor_trigger_chan broadcast → fake transport forwards → data appears
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include <fake_remote_sensor/fake_remote_sensor.h>
#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

/* --------------------------------------------------------------------------
 * Test infrastructure
 * -------------------------------------------------------------------------- */

/* Semaphore + copy of the last received event. */
static K_SEM_DEFINE(event_sem, 0, 8);
static struct env_sensor_data last_event;

static void test_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	last_event = *evt;
	k_sem_give(&event_sem);
}

ZBUS_LISTENER_DEFINE(test_event_listener, test_event_cb);

/* --------------------------------------------------------------------------
 * Suite setup
 * -------------------------------------------------------------------------- */

static void *suite_setup(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &test_event_listener,
				  K_NO_WAIT);

	zassert_ok(rc, "Failed to add sensor_event observer: %d", rc);
	return NULL;
}

static void before_each(void *f)
{
	ARG_UNUSED(f);
	k_sem_reset(&event_sem);
}

ZTEST_SUITE(remote_sensor_manager_suite, NULL, suite_setup, before_each,
	    NULL, NULL);

/* --------------------------------------------------------------------------
 * t1: transport registered in iterable section
 * -------------------------------------------------------------------------- */

/**
 * @brief STRUCT_SECTION_FOREACH must find at least the fake transport.
 *
 * Validates that REMOTE_TRANSPORT_DEFINE() correctly places the vtable in
 * the remote_transport iterable linker section.
 */
ZTEST(remote_sensor_manager_suite, test_fake_transport_registered)
{
	int count = 0;
	bool found_fake = false;

	STRUCT_SECTION_FOREACH(remote_transport, t) {
		count++;
		if (t->proto == REMOTE_TRANSPORT_PROTO_FAKE) {
			found_fake = true;
			zassert_not_null(t->name,
					 "transport name must not be NULL");
			zassert_true(
				t->caps & REMOTE_TRANSPORT_CAP_SCAN,
				"fake transport must have SCAN capability");
			zassert_true(
				t->caps & REMOTE_TRANSPORT_CAP_TRIGGER,
				"fake transport must have TRIGGER capability");
		}
	}

	zassert_true(count > 0,
		     "No transports found in iterable section");
	zassert_true(found_fake,
		     "fake transport (proto=FAKE) not found in iterable section");
}

/* --------------------------------------------------------------------------
 * t2: discovery → sensor_registry registration
 * -------------------------------------------------------------------------- */

/**
 * @brief Announcing fake sensors must register them in sensor_registry.
 *
 * Steps:
 *   1. Ensure no remote sensors are registered yet.
 *   2. Call fake_remote_sensor_announce().
 *   3. Wait for the manager subscriber thread to process the events.
 *   4. Query sensor_registry — both UIDs must be present with is_remote=true.
 */
ZTEST(remote_sensor_manager_suite, test_discovery_registers_sensors)
{
	/* Derive the UIDs the fake transport will announce. */
	static const uint8_t fake_mac[] = {0xFA, 0x4E, 0x00, 0x00, 0x00, 0x00};
	uint32_t expected_uid_temp = remote_sensor_uid_from_addr(
		CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX,
		fake_mac, sizeof(fake_mac), SENSOR_TYPE_TEMPERATURE);
	uint32_t expected_uid_hum = remote_sensor_uid_from_addr(
		CONFIG_FAKE_REMOTE_SENSOR_UID_PREFIX,
		fake_mac, sizeof(fake_mac), SENSOR_TYPE_HUMIDITY);

	int rc = fake_remote_sensor_announce();

	zassert_ok(rc, "fake_remote_sensor_announce failed: %d", rc);

	/* Give the manager subscriber thread time to process both events. */
	k_sleep(K_MSEC(200));

	const struct sensor_registry_entry *entry_temp =
		sensor_registry_lookup(expected_uid_temp);
	const struct sensor_registry_entry *entry_hum =
		sensor_registry_lookup(expected_uid_hum);

	zassert_not_null(entry_temp,
			 "Temperature sensor uid=0x%08x not in registry after "
			 "announce", expected_uid_temp);
	zassert_not_null(entry_hum,
			 "Humidity sensor uid=0x%08x not in registry after "
			 "announce", expected_uid_hum);

	zassert_true(entry_temp->is_remote,
		     "Temperature entry must have is_remote=true");
	zassert_true(entry_hum->is_remote,
		     "Humidity entry must have is_remote=true");

	/* Re-announcing the same sensors must be idempotent. */
	int count_before = sensor_registry_count();

	rc = fake_remote_sensor_announce();
	zassert_ok(rc, "second announce failed: %d", rc);
	k_sleep(K_MSEC(100));

	zassert_equal(sensor_registry_count(), count_before,
		      "Registry count changed after duplicate announce "
		      "(was %d, now %d)", count_before,
		      sensor_registry_count());
}

/* --------------------------------------------------------------------------
 * t3: publish_all → sensor_event_chan
 * -------------------------------------------------------------------------- */

/**
 * @brief fake_remote_sensor_publish_all() must emit events on sensor_event_chan.
 *
 * Requires sensors to be registered first (calls announce if needed).
 * Checks that at least one event arrives and has valid fields.
 */
ZTEST(remote_sensor_manager_suite, test_publish_produces_event)
{
	/* Ensure sensors are registered. */
	fake_remote_sensor_announce();
	k_sleep(K_MSEC(200));

	k_sem_reset(&event_sem);

	fake_remote_sensor_publish_all();

	/* Expect at least one event within 1 s. */
	int rc = k_sem_take(&event_sem, K_SECONDS(1));

	zassert_ok(rc,
		   "No sensor_event received within 1 s after publish_all");

	zassert_true(last_event.sensor_uid != 0,
		     "Event has sensor_uid == 0");
	zassert_true(last_event.type == SENSOR_TYPE_TEMPERATURE ||
			     last_event.type == SENSOR_TYPE_HUMIDITY,
		     "Event type %d is not TEMPERATURE or HUMIDITY",
		     last_event.type);
	zassert_true(last_event.timestamp_ms > 0,
		     "Event timestamp_ms must be positive");
}

/* --------------------------------------------------------------------------
 * t4: sensor_trigger_chan broadcast → trigger forwarded → data appears
 * -------------------------------------------------------------------------- */

/**
 * @brief A broadcast sensor_trigger_event must cause the remote_sensor_manager
 *        to call send_trigger on the fake transport, which publishes data.
 *
 * This exercises the full trigger-routing path:
 *   sensor_trigger_chan → remote_trigger_listener → find_transport →
 *   fake_send_trigger → publish_node → sensor_event_chan
 */
ZTEST(remote_sensor_manager_suite, test_trigger_routing)
{
	/* Ensure sensors are registered. */
	fake_remote_sensor_announce();
	k_sleep(K_MSEC(200));

	k_sem_reset(&event_sem);

	/* Broadcast trigger — target_uid=0 means all sensors. */
	struct sensor_trigger_event trig = {
		.source = TRIGGER_SOURCE_TIMER,
		.target_uid = 0,
	};
	int rc = zbus_chan_pub(&sensor_trigger_chan, &trig, K_MSEC(100));

	zassert_ok(rc, "Failed to publish trigger: %d", rc);

	/* The listener is synchronous — give it a short moment to fire. */
	rc = k_sem_take(&event_sem, K_SECONDS(1));
	zassert_ok(rc,
		   "No sensor_event received within 1 s after trigger");

	zassert_true(last_event.sensor_uid != 0,
		     "Triggered event has sensor_uid == 0");
}

/* --------------------------------------------------------------------------
 * t5: published event has remote sensor UID registered in registry
 * -------------------------------------------------------------------------- */

/**
 * @brief The UID in each event from a remote sensor must be in sensor_registry.
 *
 * Drains up to 4 events from publish_all and checks each one.
 */
ZTEST(remote_sensor_manager_suite, test_event_uid_in_registry)
{
	fake_remote_sensor_announce();
	k_sleep(K_MSEC(200));

	k_sem_reset(&event_sem);
	fake_remote_sensor_publish_all();

	/* Collect up to 4 events (2 types × up to 2 nodes). */
	for (int i = 0; i < 4; i++) {
		int rc = k_sem_take(&event_sem, K_MSEC(500));

		if (rc != 0) {
			break; /* No more events. */
		}

		const struct sensor_registry_entry *entry =
			sensor_registry_lookup(last_event.sensor_uid);

		zassert_not_null(
			entry,
			"Event uid=0x%08x not found in sensor_registry",
			last_event.sensor_uid);

		zassert_true(entry->is_remote,
			     "Event uid=0x%08x has is_remote=false",
			     last_event.sensor_uid);
	}
}
