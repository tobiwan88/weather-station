/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sensor_event.h
 * @brief Sensor event channel and data types for the weather-station.
 *
 * One env_sensor_data event represents exactly one physical measurement.
 * Temperature and humidity from the same chip are published as two
 * separate events on sensor_event_chan.
 *
 * Q31 encoding:
 *   temperature: range -40..+85 °C  → q31 = (t + 40) / 125 * INT32_MAX
 *   humidity:    range 0..100 %RH   → q31 = h / 100 * INT32_MAX
 */

#ifndef SENSOR_EVENT_SENSOR_EVENT_H_
#define SENSOR_EVENT_SENSOR_EVENT_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Physical quantity represented by the event. */
enum sensor_type {
	SENSOR_TYPE_TEMPERATURE, /**< Degrees Celsius                 */
	SENSOR_TYPE_HUMIDITY,    /**< Relative humidity, %RH          */
	SENSOR_TYPE_PRESSURE,    /**< Atmospheric pressure, hPa       */
	SENSOR_TYPE_CO2,         /**< CO₂ concentration, ppm          */
	SENSOR_TYPE_LIGHT,       /**< Illuminance, lux                */
	SENSOR_TYPE_UV_INDEX,    /**< UV index (dimensionless)        */
	SENSOR_TYPE_BATTERY_MV,  /**< Battery voltage, millivolts     */
};

/**
 * @brief Sensor measurement event transmitted on sensor_event_chan.
 *
 * Size on 32-bit: 20 bytes. Size on 64-bit: 24 bytes (4-byte trailing
 * padding due to int64_t alignment).  No pointers; safe to copy.
 */
struct env_sensor_data {
	uint32_t sensor_uid;   /**< DT-assigned unique sensor identifier   */
	enum sensor_type type; /**< Physical quantity (enum is 32-bit)     */
	int32_t q31_value;     /**< Q31 fixed-point encoded measurement    */
	int64_t timestamp_ms;  /**< k_uptime_get() at sample time, ms      */
};

/** zbus channel carrying env_sensor_data events (defined in sensor_event.c). */
ZBUS_CHAN_DECLARE(sensor_event_chan);

/* --------------------------------------------------------------------------
 * Q31 conversion helpers
 * --------------------------------------------------------------------------
 * Q31 range: [INT32_MIN, INT32_MAX] represents [-1.0, +1.0).
 * We map the physical range onto this interval.
 *
 * Temperature: phys ∈ [-40, +85] °C, span = 125 °C
 *   encode: q31 = (t_c + 40.0) / 125.0 * INT32_MAX
 *   decode: t_c = (double)q31 / INT32_MAX * 125.0 - 40.0
 *
 * Humidity: phys ∈ [0, 100] %RH, span = 100 %RH
 *   encode: q31 = h_pct / 100.0 * INT32_MAX
 *   decode: h_pct = (double)q31 / INT32_MAX * 100.0
 * -------------------------------------------------------------------------- */

/**
 * @brief Encode a temperature in degrees Celsius to Q31.
 * @param t_c Temperature in °C (range -40 .. +85).
 * @return Q31 encoded value.
 */
static inline int32_t temperature_c_to_q31(double t_c)
{
	return (int32_t)((t_c + 40.0) / 125.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to temperature in degrees Celsius.
 * @param q31 Q31 encoded temperature.
 * @return Temperature in °C.
 */
static inline double q31_to_temperature_c(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 125.0 - 40.0;
}

/**
 * @brief Encode a relative humidity percentage to Q31.
 * @param h_pct Humidity in %RH (range 0 .. 100).
 * @return Q31 encoded value.
 */
static inline int32_t humidity_pct_to_q31(double h_pct)
{
	return (int32_t)(h_pct / 100.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to relative humidity percentage.
 * @param q31 Q31 encoded humidity.
 * @return Humidity in %RH.
 */
static inline double q31_to_humidity_pct(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 100.0;
}

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_EVENT_SENSOR_EVENT_H_ */
