/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/mqtt_publisher)
 * @brief Tests for the mqtt_publisher library.
 *
 * Two suites:
 *
 *   format_suite — unit tests for the pure topic/payload helpers in
 *     mqtt_publisher_format.c.  No MQTT broker, no sensor_registry, no RTOS
 *     state needed.
 *
 *   integration_suite — verifies that a sensor_event_chan publish reaches the
 *     publisher's internal queue.  Uses two fake sensor DT nodes
 *     (0x0201 temperature, 0x0202 humidity) from the DT overlay and calls
 *     mqtt_publisher_queue_used() to check without a live broker.
 */

#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include <mqtt_publisher/mqtt_publisher.h>
#include <sensor_event/sensor_event.h>
#include <sensor_trigger/sensor_trigger.h>

/* Internal format helpers (src/ is added to include path in CMakeLists). */
#include "mqtt_publisher_format.h"

/* ============================================================================
 * Suite 1: format unit tests
 * ============================================================================ */

ZTEST_SUITE(format_suite, NULL, NULL, NULL, NULL, NULL);

/* --------------------------------------------------------------------------
 * type_to_topic_str
 * -------------------------------------------------------------------------- */

ZTEST(format_suite, test_topic_str_temperature)
{
	zassert_str_equal(mqtt_publisher_type_to_topic_str(SENSOR_TYPE_TEMPERATURE), "temperature",
			  "TEMPERATURE topic str mismatch");
}

ZTEST(format_suite, test_topic_str_humidity)
{
	zassert_str_equal(mqtt_publisher_type_to_topic_str(SENSOR_TYPE_HUMIDITY), "humidity",
			  "HUMIDITY topic str mismatch");
}

ZTEST(format_suite, test_topic_str_pressure)
{
	zassert_str_equal(mqtt_publisher_type_to_topic_str(SENSOR_TYPE_PRESSURE), "pressure",
			  "PRESSURE topic str mismatch");
}

ZTEST(format_suite, test_topic_str_co2)
{
	zassert_str_equal(mqtt_publisher_type_to_topic_str(SENSOR_TYPE_CO2), "co2",
			  "CO2 topic str mismatch");
}

ZTEST(format_suite, test_topic_str_battery_mv)
{
	zassert_str_equal(mqtt_publisher_type_to_topic_str(SENSOR_TYPE_BATTERY_MV), "battery_mv",
			  "BATTERY_MV topic str mismatch");
}

/* --------------------------------------------------------------------------
 * sensor_type_to_unit (from sensor_event lib)
 * -------------------------------------------------------------------------- */

ZTEST(format_suite, test_unit_temperature)
{
	/* °C is encoded as UTF-8 \xc2\xb0\x43 */
	const char *u = sensor_type_to_unit(SENSOR_TYPE_TEMPERATURE);

	zassert_not_null(u, "unit must not be NULL");
	zassert_true(strlen(u) > 0, "temperature unit must not be empty");
	/* Verify the degree symbol is present (UTF-8 0xC2 0xB0) */
	zassert_equal((unsigned char)u[0], 0xC2u,
		      "temperature unit first byte expected 0xC2, got 0x%02X", (unsigned char)u[0]);
}

ZTEST(format_suite, test_unit_humidity)
{
	zassert_str_equal(sensor_type_to_unit(SENSOR_TYPE_HUMIDITY), "%", "HUMIDITY unit mismatch");
}

ZTEST(format_suite, test_unit_pressure)
{
	zassert_str_equal(sensor_type_to_unit(SENSOR_TYPE_PRESSURE), "hPa",
			  "PRESSURE unit mismatch");
}

ZTEST(format_suite, test_unit_co2)
{
	zassert_str_equal(sensor_type_to_unit(SENSOR_TYPE_CO2), "ppm", "CO2 unit mismatch");
}

ZTEST(format_suite, test_unit_battery_mv)
{
	zassert_str_equal(sensor_type_to_unit(SENSOR_TYPE_BATTERY_MV), "mV",
			  "BATTERY_MV unit mismatch");
}

/* --------------------------------------------------------------------------
 * build_topic
 * -------------------------------------------------------------------------- */

ZTEST(format_suite, test_topic_normal)
{
	char buf[128];

	mqtt_publisher_build_topic("gw", "living-room", "dht22", SENSOR_TYPE_TEMPERATURE, buf,
				   sizeof(buf));

	zassert_str_equal(buf, "gw/living-room/dht22/temperature", "unexpected topic: '%s'", buf);
}

ZTEST(format_suite, test_topic_null_location_fallback)
{
	char buf[128];

	mqtt_publisher_build_topic("gw", NULL, "sensor1", SENSOR_TYPE_HUMIDITY, buf, sizeof(buf));

	zassert_str_equal(buf, "gw/unknown/sensor1/humidity",
			  "NULL location should produce 'unknown', got: '%s'", buf);
}

ZTEST(format_suite, test_topic_empty_location_fallback)
{
	char buf[128];

	mqtt_publisher_build_topic("gw", "", "sensor1", SENSOR_TYPE_HUMIDITY, buf, sizeof(buf));

	zassert_str_equal(buf, "gw/unknown/sensor1/humidity",
			  "empty location should produce 'unknown', got: '%s'", buf);
}

ZTEST(format_suite, test_topic_null_name_fallback)
{
	char buf[128];

	mqtt_publisher_build_topic("gw", "kitchen", NULL, SENSOR_TYPE_TEMPERATURE, buf,
				   sizeof(buf));

	zassert_str_equal(buf, "gw/kitchen/unknown/temperature",
			  "NULL name should produce 'unknown', got: '%s'", buf);
}

ZTEST(format_suite, test_topic_empty_name_fallback)
{
	char buf[128];

	mqtt_publisher_build_topic("gw", "kitchen", "", SENSOR_TYPE_TEMPERATURE, buf, sizeof(buf));

	zassert_str_equal(buf, "gw/kitchen/unknown/temperature",
			  "empty name should produce 'unknown', got: '%s'", buf);
}

ZTEST(format_suite, test_topic_no_double_slash)
{
	char buf[128];

	mqtt_publisher_build_topic("weather", "office", "bme280", SENSOR_TYPE_PRESSURE, buf,
				   sizeof(buf));

	zassert_is_null(strstr(buf, "//"), "topic must not contain '//': '%s'", buf);
}

/* --------------------------------------------------------------------------
 * build_payload
 * -------------------------------------------------------------------------- */

ZTEST(format_suite, test_payload_keys_present)
{
	char buf[256];
	int32_t q31 = temperature_c_to_q31(22.0);

	int len = mqtt_publisher_build_payload(1000000LL, SENSOR_TYPE_TEMPERATURE, q31, buf,
					       sizeof(buf));

	zassert_true(len > 0, "build_payload returned %d", len);
	zassert_not_null(strstr(buf, "\"time\""), "payload missing 'time' key");
	zassert_not_null(strstr(buf, "\"value\""), "payload missing 'value' key");
	zassert_not_null(strstr(buf, "\"unit\""), "payload missing 'unit' key");
}

ZTEST(format_suite, test_payload_epoch_seconds)
{
	char buf[256];
	int32_t q31 = temperature_c_to_q31(0.0);

	mqtt_publisher_build_payload(1743948000LL, SENSOR_TYPE_TEMPERATURE, q31, buf, sizeof(buf));

	/* The epoch value must appear literally in the payload. */
	zassert_not_null(strstr(buf, "1743948000"), "epoch_s not found in payload: '%s'", buf);
}

ZTEST(format_suite, test_payload_temperature_value)
{
	char buf[256];
	const double input_c = 23.45;
	int32_t q31 = temperature_c_to_q31(input_c);

	mqtt_publisher_build_payload(0LL, SENSOR_TYPE_TEMPERATURE, q31, buf, sizeof(buf));

	/* Value should be 23.45 — accept ±0.1 °C rounding in snprintf("%.2f") */
	zassert_not_null(strstr(buf, "23.4"), "temperature value not found in payload: '%s'", buf);
}

ZTEST(format_suite, test_payload_humidity_value)
{
	char buf[256];
	const double input_pct = 65.2;
	int32_t q31 = humidity_pct_to_q31(input_pct);

	mqtt_publisher_build_payload(0LL, SENSOR_TYPE_HUMIDITY, q31, buf, sizeof(buf));

	zassert_not_null(strstr(buf, "65."), "humidity value not found in payload: '%s'", buf);
}

ZTEST(format_suite, test_payload_is_valid_json_start_end)
{
	char buf[256];

	mqtt_publisher_build_payload(0LL, SENSOR_TYPE_HUMIDITY, humidity_pct_to_q31(50.0), buf,
				     sizeof(buf));

	zassert_equal(buf[0], '{', "payload must start with '{'");
	zassert_equal(buf[strlen(buf) - 1], '}', "payload must end with '}'");
}

/* ============================================================================
 * Suite 2: integration — zbus trigger → queue enqueue
 * ============================================================================ */

ZTEST_SUITE(integration_suite, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Publishing on sensor_trigger_chan must enqueue items in the
 *        mqtt_publisher's internal queue within 500 ms.
 *
 * Two fake sensor DT nodes (0x0201, 0x0202) from the overlay respond to the
 * broadcast trigger and emit events on sensor_event_chan.  The publisher's
 * zbus listener picks them up and enqueues them.  We check
 * mqtt_publisher_queue_used() > 0 without needing a live broker.
 *
 * NOTE: the MQTT thread will attempt to connect (and fail — no broker in CI).
 * Events are enqueued regardless; they will be dropped when the thread
 * re-enters the reconnect loop.  This test only validates the listener path.
 */
ZTEST(integration_suite, test_trigger_enqueues_events)
{
	/* Drain any events queued from startup auto-publish. */
	k_sleep(K_MSEC(100));

	/* Record baseline. */
	int before = mqtt_publisher_queue_used();

	/* Publish a broadcast trigger — both fake sensors will respond. */
	struct sensor_trigger_event trig = {
		.source = TRIGGER_SOURCE_TIMER,
		.target_uid = 0,
	};

	int rc = zbus_chan_pub(&sensor_trigger_chan, &trig, K_MSEC(100));

	zassert_ok(rc, "Failed to publish trigger: %d", rc);

	/* Give the sensor drivers and zbus dispatch time to run. */
	k_sleep(K_MSEC(200));

	int after = mqtt_publisher_queue_used();

	zassert_true(after > before,
		     "Queue count did not increase after trigger "
		     "(before=%d after=%d) — listener may not be registered",
		     before, after);
}
