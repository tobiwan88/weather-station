/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/sensor_node_tx)
 * @brief Batching tests for lib/sensor_node_tx.
 *
 * LORA_NODE is not enabled. The library source is compiled directly (see
 * CMakeLists.txt) and lora_node_init() / lora_node_transmit() are mocked here.
 * The periodic timer is set to 3600 s, so only forced TX runs during the tests.
 *
 * The mock records the number of readings per call and the decoded wire
 * payload, and can fail a chosen call (1-based) with -EIO.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include <lora_node/lora_node.h>
#include <sensor_event/sensor_event.h>
#include <sensor_node_tx/sensor_node_tx.h>

/* Observer defined (non-static) in lib/sensor_node_tx/src/sensor_node_tx.c */
extern const struct zbus_observer sensor_node_tx_listener;

#define MAX_READINGS CONFIG_SENSOR_NODE_TX_MAX_READINGS
#define PER_PACKET   CONFIG_SENSOR_NODE_TX_READINGS_PER_PACKET
#define WIRE_SIZE    5
#define MAX_CALLS    16
#define RX_LOG_SIZE  64

/* --------------------------------------------------------------------------
 * lora_node mock
 * -------------------------------------------------------------------------- */

static K_SEM_DEFINE(tx_call_sem, 0, MAX_CALLS);
static int tx_calls;
static size_t tx_sizes[MAX_CALLS];
static struct {
	uint8_t type;
	int32_t q31;
} rx_log[RX_LOG_SIZE];
static size_t rx_count;
static int fail_on_call; /* 1-based call index to fail, 0 = never */

int lora_node_init(void)
{
	return 0;
}

int lora_node_transmit(const uint8_t *readings, size_t num_readings)
{
	int ret = 0;

	tx_calls++;
	if (tx_calls <= MAX_CALLS) {
		tx_sizes[tx_calls - 1] = num_readings;
	}

	for (size_t i = 0; i < num_readings; i++) {
		if (rx_count >= RX_LOG_SIZE) {
			break;
		}
		const uint8_t *w = &readings[i * WIRE_SIZE];

		rx_log[rx_count].type = w[0];
		memcpy(&rx_log[rx_count].q31, &w[1], sizeof(int32_t));
		rx_count++;
	}

	if (tx_calls == fail_on_call) {
		ret = -EIO;
	}

	k_sem_give(&tx_call_sem);
	return ret;
}

static void mock_reset(void)
{
	k_sem_reset(&tx_call_sem);
	tx_calls = 0;
	rx_count = 0;
	memset(tx_sizes, 0, sizeof(tx_sizes));
	fail_on_call = 0;
}

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void publish_reading(int32_t q31)
{
	struct env_sensor_data evt = {
		.sensor_uid = 0x0001,
		.type = SENSOR_TYPE_TEMPERATURE,
		.q31_value = q31,
		.timestamp_ms = k_uptime_get(),
	};

	zassert_ok(zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT));
}

/* Wait for exactly `expected` lora_node_transmit() calls, then give the work
 * queue time to make any unexpected extra call before checking the count.
 */
static void wait_for_calls(int expected)
{
	for (int i = 0; i < expected; i++) {
		zassert_equal(k_sem_take(&tx_call_sem, K_SECONDS(2)), 0,
			      "timed out waiting for transmit call %d of %d", i + 1, expected);
	}

	k_sleep(K_MSEC(50));
	zassert_equal(tx_calls, expected, "transmit called %d times, expected %d", tx_calls,
		      expected);
}

/* Verify that no transmit call happened beyond `total` */
static void expect_no_more_calls(int total)
{
	k_sleep(K_MSEC(100));
	zassert_equal(tx_calls, total, "transmit called %d times, expected %d", tx_calls, total);
}

static void check_reading(size_t idx, int32_t expected_q31)
{
	zassert_true(idx < rx_count, "reading %zu not transmitted (rx_count=%zu)", idx, rx_count);
	zassert_equal(rx_log[idx].q31, expected_q31, "reading %zu q31=%d, expected %d", idx,
		      rx_log[idx].q31, expected_q31);
	zassert_equal(rx_log[idx].type, SENSOR_TYPE_TEMPERATURE, "reading %zu wrong type", idx);
}

/* --------------------------------------------------------------------------
 * Suite setup
 * -------------------------------------------------------------------------- */

static void *suite_setup(void)
{
	/* The library must attach its own listener at SYS_INIT; adding it again
	 * fails with -EALREADY. (Regression: it once never observed the channel.)
	 */
	int rc = zbus_chan_add_obs(&sensor_event_chan, &sensor_node_tx_listener, K_NO_WAIT);

	zassert_equal(rc, -EALREADY, "sensor_node_tx listener not attached by the library (%d)",
		      rc);
	return NULL;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Drain anything left over from a previous test, then reset the mock */
	mock_reset();
	zassert_ok(sensor_node_tx_force_tx());
	k_sleep(K_MSEC(100));
	mock_reset();
}

/* --------------------------------------------------------------------------
 * Tests
 * -------------------------------------------------------------------------- */

/* 12 readings with a packet size of 5 go out as 5, 5, 2 */
ZTEST(sensor_node_tx, test_batch_split_into_packets)
{
	for (int32_t i = 0; i < 12; i++) {
		publish_reading(100 + i);
	}

	zassert_ok(sensor_node_tx_force_tx());
	wait_for_calls(3);

	zassert_equal(tx_sizes[0], 5, "call 1 size");
	zassert_equal(tx_sizes[1], 5, "call 2 size");
	zassert_equal(tx_sizes[2], 2, "call 3 size");
	zassert_equal(rx_count, 12, "total readings transmitted");

	for (size_t i = 0; i < 12; i++) {
		check_reading(i, 100 + (int32_t)i);
	}

	/* Buffer is empty now, so another force TX must not call the radio */
	zassert_ok(sensor_node_tx_force_tx());
	expect_no_more_calls(3);
}

/* Failing the 2nd chunk keeps its 5 readings and the 2 untried ones buffered */
ZTEST(sensor_node_tx, test_failed_chunk_is_requeued)
{
	for (int32_t i = 0; i < 12; i++) {
		publish_reading(100 + i);
	}

	fail_on_call = 2;
	zassert_ok(sensor_node_tx_force_tx());
	wait_for_calls(2);

	/* Call 1 (5 readings) succeeded, call 2 (5 readings) failed */
	zassert_equal(tx_sizes[0], 5, "call 1 size");
	zassert_equal(tx_sizes[1], 5, "call 2 size");

	/* Retry with the mock healthy: the 7 requeued readings go out as 5, 2 */
	mock_reset();
	zassert_ok(sensor_node_tx_force_tx());
	wait_for_calls(2);

	zassert_equal(tx_sizes[0], 5, "retry call 1 size");
	zassert_equal(tx_sizes[1], 2, "retry call 2 size");
	zassert_equal(rx_count, 7, "retried readings");

	for (size_t i = 0; i < 7; i++) {
		check_reading(i, 105 + (int32_t)i);
	}
}

/* Publishing more than MAX_READINGS must cap the buffer and keep the newest
 * values (oldest dropped).
 */
ZTEST(sensor_node_tx, test_overflow_drops_oldest)
{
	const int32_t total = MAX_READINGS + 5;

	for (int32_t i = 0; i < total; i++) {
		publish_reading(100 + i);
	}

	zassert_ok(sensor_node_tx_force_tx());
	wait_for_calls(MAX_READINGS / PER_PACKET);
	zassert_equal(rx_count, MAX_READINGS, "buffer should cap at MAX_READINGS");

	for (size_t i = 0; i < MAX_READINGS / PER_PACKET; i++) {
		zassert_equal(tx_sizes[i], PER_PACKET, "call %zu size", i + 1);
	}

	/* Newest MAX_READINGS values are 105..124 */
	check_reading(0, 100 + (int32_t)(total - MAX_READINGS));
	check_reading(MAX_READINGS - 1, 100 + total - 1);
}

ZTEST_SUITE(sensor_node_tx, NULL, suite_setup, before_each, NULL, NULL);
