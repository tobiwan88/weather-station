/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (test/sensor_event)
 * @brief Unit tests for Q31 helpers and env_sensor_data layout.
 */

#include <limits.h>
#include <math.h>
#include <string.h>
#include <sensor_event/sensor_event.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(sensor_event_suite, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Round-trip encode/decode of a typical temperature value.
 *
 * 21.5 °C → temperature_c_to_q31() → q31_to_temperature_c()
 * Error must be less than 0.01 °C.
 */
ZTEST(sensor_event_suite, test_q31_temperature_roundtrip)
{
	const double input_c = 21.5;
	int32_t encoded = temperature_c_to_q31(input_c);
	double decoded_c = q31_to_temperature_c(encoded);
	double error = fabs(decoded_c - input_c);

	zassert_true(error < 0.01,
		     "Temperature round-trip error %.6f °C exceeds 0.01 °C "
		     "(in=%.3f encoded=0x%08x decoded=%.6f)",
		     error, input_c, encoded, decoded_c);
}

/**
 * @brief Round-trip encode/decode of a typical humidity value.
 *
 * 55.0 %RH → humidity_pct_to_q31() → q31_to_humidity_pct()
 * Error must be less than 0.01 %RH.
 */
ZTEST(sensor_event_suite, test_q31_humidity_roundtrip)
{
	const double input_pct = 55.0;
	int32_t encoded = humidity_pct_to_q31(input_pct);
	double decoded_pct = q31_to_humidity_pct(encoded);
	double error = fabs(decoded_pct - input_pct);

	zassert_true(error < 0.01,
		     "Humidity round-trip error %.6f %%RH exceeds 0.01 %%RH "
		     "(in=%.3f encoded=0x%08x decoded=%.6f)",
		     error, input_pct, encoded, decoded_pct);
}

/**
 * @brief Guard against accidental growth of env_sensor_data.
 *
 * This is an in-memory zbus message, not a wire format.  Cross-device
 * serialisation uses protobuf or a similar encoding layer (see backlog).
 *
 * Natural size on native_sim/native/64: 24 bytes
 *   uint32_t(4) + enum(4) + int32_t(4) + [4-byte pad] + int64_t(8) = 24.
 */
ZTEST(sensor_event_suite, test_env_sensor_data_size)
{
	zassert_equal(sizeof(struct env_sensor_data), 24,
		      "env_sensor_data size is %zu, expected 24", sizeof(struct env_sensor_data));
}

/**
 * @brief Q31 encode of -40 °C (bottom of range) must equal 0.
 *
 * Formula: q31 = (t + 40) / 125 * INT32_MAX → (-40 + 40) / 125 = 0.
 */
ZTEST(sensor_event_suite, test_q31_temperature_min_roundtrip)
{
	const double input_c = -40.0;
	int32_t encoded = temperature_c_to_q31(input_c);

	zassert_equal(encoded, 0, "temperature_c_to_q31(-40) should be 0, got %d", encoded);

	double decoded_c = q31_to_temperature_c(encoded);
	double error = fabs(decoded_c - input_c);

	zassert_true(error < 0.01,
		     "Temperature min round-trip error %.6f °C exceeds 0.01 °C", error);
}

/**
 * @brief Q31 encode of +85 °C (top of range) must equal INT32_MAX.
 *
 * Formula: q31 = (85 + 40) / 125 * INT32_MAX = 1.0 * INT32_MAX.
 */
ZTEST(sensor_event_suite, test_q31_temperature_max_roundtrip)
{
	const double input_c = 85.0;
	int32_t encoded = temperature_c_to_q31(input_c);

	zassert_equal(encoded, INT32_MAX,
		      "temperature_c_to_q31(85) should be INT32_MAX, got %d", encoded);

	double decoded_c = q31_to_temperature_c(encoded);
	double error = fabs(decoded_c - input_c);

	zassert_true(error < 0.01,
		     "Temperature max round-trip error %.6f °C exceeds 0.01 °C", error);
}

/**
 * @brief Q31 encode of 0 %RH (minimum humidity) must equal 0.
 */
ZTEST(sensor_event_suite, test_q31_humidity_min_roundtrip)
{
	const double input_pct = 0.0;
	int32_t encoded = humidity_pct_to_q31(input_pct);

	zassert_equal(encoded, 0, "humidity_pct_to_q31(0) should be 0, got %d", encoded);

	double decoded_pct = q31_to_humidity_pct(encoded);
	double error = fabs(decoded_pct - input_pct);

	zassert_true(error < 0.01,
		     "Humidity min round-trip error %.6f %%RH exceeds 0.01 %%RH", error);
}

/**
 * @brief Q31 encode of 100 %RH (maximum humidity) must equal INT32_MAX.
 */
ZTEST(sensor_event_suite, test_q31_humidity_max_roundtrip)
{
	const double input_pct = 100.0;
	int32_t encoded = humidity_pct_to_q31(input_pct);

	zassert_equal(encoded, INT32_MAX,
		      "humidity_pct_to_q31(100) should be INT32_MAX, got %d", encoded);

	double decoded_pct = q31_to_humidity_pct(encoded);
	double error = fabs(decoded_pct - input_pct);

	zassert_true(error < 0.01,
		     "Humidity max round-trip error %.6f %%RH exceeds 0.01 %%RH", error);
}

/**
 * @brief All sensor_type enum values must be pairwise distinct.
 *
 * Guards against accidental enum value collisions when new types are added.
 */
ZTEST(sensor_event_suite, test_sensor_type_enum_values_distinct)
{
	zassert_not_equal(SENSOR_TYPE_TEMPERATURE, SENSOR_TYPE_HUMIDITY,
			  "TEMPERATURE and HUMIDITY must differ");
	zassert_not_equal(SENSOR_TYPE_TEMPERATURE, SENSOR_TYPE_PRESSURE,
			  "TEMPERATURE and PRESSURE must differ");
	zassert_not_equal(SENSOR_TYPE_HUMIDITY, SENSOR_TYPE_CO2,
			  "HUMIDITY and CO2 must differ");
	zassert_not_equal(SENSOR_TYPE_PRESSURE, SENSOR_TYPE_CO2,
			  "PRESSURE and CO2 must differ");
	zassert_not_equal(SENSOR_TYPE_LIGHT, SENSOR_TYPE_UV_INDEX,
			  "LIGHT and UV_INDEX must differ");
	zassert_not_equal(SENSOR_TYPE_UV_INDEX, SENSOR_TYPE_BATTERY_MV,
			  "UV_INDEX and BATTERY_MV must differ");
}

/**
 * @brief env_sensor_data must survive a memcpy with all fields intact.
 *
 * Verifies the struct has no-pointer copy semantics as documented —
 * essential for safe zbus in-process message passing.
 */
ZTEST(sensor_event_suite, test_env_sensor_data_memcpy)
{
	struct env_sensor_data src = {
		.sensor_uid = 0xCAFE0001U,
		.type = SENSOR_TYPE_TEMPERATURE,
		.q31_value = temperature_c_to_q31(25.0),
		.timestamp_ms = 12345LL,
	};
	struct env_sensor_data dst;

	memcpy(&dst, &src, sizeof(dst));

	zassert_equal(dst.sensor_uid, src.sensor_uid, "sensor_uid mismatch after memcpy");
	zassert_equal(dst.type, src.type, "type mismatch after memcpy");
	zassert_equal(dst.q31_value, src.q31_value, "q31_value mismatch after memcpy");
	zassert_equal(dst.timestamp_ms, src.timestamp_ms, "timestamp_ms mismatch after memcpy");
}
