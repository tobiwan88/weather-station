/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (test/fake_sensors)
 * @brief Integration tests for the fake_sensors module.
 *
 * Two DT nodes are defined in boards/native_sim.overlay:
 *   fake_temp_test (uid=0x0101, "fake,temperature")
 *   fake_hum_test  (uid=0x0102, "fake,humidity")
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include <fake_sensors/fake_sensors.h>
#include <sensor_event/sensor_event.h>
#include <sensor_trigger/sensor_trigger.h>

/* Semaphore posted by the test's event listener when an event arrives. */
static K_SEM_DEFINE(event_received_sem, 0, 1);
static struct env_sensor_data last_event;

static void test_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	last_event = *evt;
	k_sem_give(&event_received_sem);
}

ZBUS_LISTENER_DEFINE(test_event_listener, test_event_cb);

/* ── Suite setup / teardown ─────────────────────────────────────────────── */

static void *suite_setup(void)
{
	/* Subscribe test listener to sensor_event_chan. */
	int rc = zbus_chan_add_obs(&sensor_event_chan, &test_event_listener, K_NO_WAIT);
	zassert_ok(rc, "Failed to add event observer: %d", rc);
	return NULL;
}

ZTEST_SUITE(fake_sensors_suite, NULL, suite_setup, NULL, NULL, NULL);

/* ── Test cases ──────────────────────────────────────────────────────────── */

/**
 * @brief Setting a non-zero auto-publish interval causes the timer to fire
 *        and produce a sensor event.
 */
ZTEST(fake_sensors_suite, test_auto_publish_fires_event)
{
	/* Ensure any previous timer is stopped and semaphore is drained. */
	fake_sensors_set_auto_publish_ms(0);
	k_sem_reset(&event_received_sem);

	/* Start at 100 ms. */
	fake_sensors_set_auto_publish_ms(100);

	int rc = k_sem_take(&event_received_sem, K_SECONDS(1));

	/* Stop before asserting to avoid leaking timer into subsequent tests. */
	fake_sensors_set_auto_publish_ms(0);

	zassert_ok(rc, "No event received within 1 s after starting auto-publish (rc=%d)", rc);
	zassert_true(last_event.sensor_uid != 0, "auto-publish event has sensor_uid == 0");
}

/**
 * @brief Setting interval to 0 stops the timer; no further events arrive.
 */
ZTEST(fake_sensors_suite, test_auto_publish_stop_suppresses_events)
{
	/* Start timer and wait for the first event to confirm it is running. */
	fake_sensors_set_auto_publish_ms(100);
	int rc = k_sem_take(&event_received_sem, K_SECONDS(1));

	zassert_ok(rc, "timer did not fire within 1 s (rc=%d)", rc);

	/* Stop the timer; let any in-flight zbus message complete. */
	fake_sensors_set_auto_publish_ms(0);
	k_msleep(50);
	k_sem_reset(&event_received_sem);

	/* No new events should arrive for the next 400 ms. */
	rc = k_sem_take(&event_received_sem, K_MSEC(400));
	zassert_equal(rc, -EAGAIN,
		      "event arrived after auto-publish stopped (rc=%d)", rc);
}

/**
 * @brief At least one fake_sensor_entry must exist in the iterable section.
 *
 * The DT overlay defines two nodes (temperature + humidity), so the count
 * must be >= 1. The exact count depends on the overlay used.
 */
ZTEST(fake_sensors_suite, test_fake_sensor_count)
{
	int count = 0;

	STRUCT_SECTION_FOREACH(fake_sensor_entry, entry)
	{
		ARG_UNUSED(entry);
		count++;
	}

	zassert_true(count > 0, "STRUCT_SECTION_COUNT(fake_sensor_entry) == 0; "
				"no fake sensors found — check DT overlay");
}

/**
 * @brief Publishing a broadcast trigger must produce at least one event on
 *        sensor_event_chan within 1 second.
 *
 * Steps:
 *   1. Drain the semaphore.
 *   2. Publish a TRIGGER_SOURCE_TIMER broadcast.
 *   3. Wait up to 1 s for the semaphore.
 *   4. Verify the received event has a valid uid and type.
 */
ZTEST(fake_sensors_suite, test_trigger_produces_event)
{
	/* Drain any leftover events from startup. */
	k_sem_reset(&event_received_sem);

	struct sensor_trigger_event trig = {
		.source = TRIGGER_SOURCE_TIMER,
		.target_uid = 0,
	};

	int rc = zbus_chan_pub(&sensor_trigger_chan, &trig, K_MSEC(100));

	zassert_ok(rc, "Failed to publish trigger: %d", rc);

	/* Wait up to 1 s for an event to arrive. */
	rc = k_sem_take(&event_received_sem, K_SECONDS(1));
	zassert_ok(rc, "No sensor event received within 1 s after trigger publish");

	/* Basic sanity on the received event. */
	zassert_true(last_event.sensor_uid != 0, "Received event has sensor_uid == 0");
	zassert_true(last_event.type == SENSOR_TYPE_TEMPERATURE ||
			     last_event.type == SENSOR_TYPE_HUMIDITY,
		     "Received event has unexpected type %d", last_event.type);
}
