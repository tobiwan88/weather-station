/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (test/sensor_event)
 * @brief Unit tests for Q31 helpers and env_sensor_data layout.
 */

#include <math.h>
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
 * @brief Verify env_sensor_data is exactly 20 bytes.
 *
 * Layout: uint32_t(4) + enum(4) + int32_t(4) + int64_t(8) = 20 bytes.
 * This must hold for correct binary transmission over LoRa.
 */
ZTEST(sensor_event_suite, test_env_sensor_data_size)
{
	zassert_equal(sizeof(struct env_sensor_data), 20,
		      "env_sensor_data size is %zu, expected 20", sizeof(struct env_sensor_data));
}
