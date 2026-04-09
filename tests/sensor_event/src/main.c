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
 * @brief Round-trip encode/decode of a typical CO₂ value.
 *
 * 800 ppm → co2_ppm_to_q31() → q31_to_co2_ppm()
 * Error must be less than 1 ppm.
 */
ZTEST(sensor_event_suite, test_q31_co2_roundtrip)
{
	const double input_ppm = 800.0;
	int32_t encoded = co2_ppm_to_q31(input_ppm);
	double decoded_ppm = q31_to_co2_ppm(encoded);
	double error = fabs(decoded_ppm - input_ppm);

	zassert_true(error < 1.0,
		     "CO2 round-trip error %.6f ppm exceeds 1 ppm "
		     "(in=%.1f encoded=0x%08x decoded=%.6f)",
		     error, input_ppm, encoded, decoded_ppm);
}

/**
 * @brief co2_ppm_to_q31() clamps out-of-range and non-finite inputs.
 */
ZTEST(sensor_event_suite, test_q31_co2_clamp)
{
	zassert_equal(co2_ppm_to_q31(-1.0), 0, "Negative ppm must clamp to 0");
	zassert_equal(co2_ppm_to_q31(6000.0), INT32_MAX, "Over-range ppm must clamp to INT32_MAX");
	zassert_equal(co2_ppm_to_q31(0.0 / 0.0), 0, "NaN must clamp to 0");
}

/**
 * @brief Round-trip encode/decode of a typical VOC IAQ value.
 *
 * 25 IAQ (Excellent) → voc_iaq_to_q31() → q31_to_voc_iaq()
 * Error must be less than 0.1 IAQ.
 */
ZTEST(sensor_event_suite, test_q31_voc_roundtrip)
{
	const double input_iaq = 25.0;
	int32_t encoded = voc_iaq_to_q31(input_iaq);
	double decoded_iaq = q31_to_voc_iaq(encoded);
	double error = fabs(decoded_iaq - input_iaq);

	zassert_true(error < 0.1,
		     "VOC round-trip error %.6f IAQ exceeds 0.1 IAQ "
		     "(in=%.1f encoded=0x%08x decoded=%.6f)",
		     error, input_iaq, encoded, decoded_iaq);
}

/**
 * @brief voc_iaq_to_q31() clamps out-of-range and non-finite inputs.
 */
ZTEST(sensor_event_suite, test_q31_voc_clamp)
{
	zassert_equal(voc_iaq_to_q31(-1.0), 0, "Negative IAQ must clamp to 0");
	zassert_equal(voc_iaq_to_q31(600.0), INT32_MAX, "Over-range IAQ must clamp to INT32_MAX");
	zassert_equal(voc_iaq_to_q31(0.0 / 0.0), 0, "NaN must clamp to 0");
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
