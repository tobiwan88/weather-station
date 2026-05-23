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
 *   pm:          range 0..1000 µg/m³ → q31 = (ugm3 / 1000.0) * INT32_MAX
 */

#ifndef SENSOR_EVENT_SENSOR_EVENT_H_
#define SENSOR_EVENT_SENSOR_EVENT_H_

#include <math.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Physical quantity represented by the event. */
enum sensor_type {
	SENSOR_TYPE_TEMPERATURE,    /**< Degrees Celsius                 */
	SENSOR_TYPE_HUMIDITY,       /**< Relative humidity, %RH          */
	SENSOR_TYPE_PRESSURE,       /**< Atmospheric pressure, hPa       */
	SENSOR_TYPE_CO2,            /**< CO₂ concentration, ppm          */
	SENSOR_TYPE_VOC,            /**< VOC air quality index (0–500)   */
	SENSOR_TYPE_LIGHT,          /**< Illuminance, lux                */
	SENSOR_TYPE_UV_INDEX,       /**< UV index (dimensionless)        */
	SENSOR_TYPE_BATTERY_MV,     /**< Battery voltage, millivolts     */
	SENSOR_TYPE_GAS_RESISTANCE, /**< Gas resistance, ohms (MOX)    */
	SENSOR_TYPE_PM1_0,          /**< PM1.0 concentration, µg/m³    */
	SENSOR_TYPE_PM2_5,          /**< PM2.5 concentration, µg/m³    */
	SENSOR_TYPE_PM10,           /**< PM10 concentration, µg/m³     */
};

/**
 * @brief Sensor measurement event transmitted on sensor_event_chan.
 *
 * In-memory transport only — no pointers, safe to memcpy within the same
 * firmware image.  Wire serialisation (cross-device, LoRa, MQTT) is handled
 * by a dedicated encoding layer (protobuf or similar); see backlog and ADR-003.
 *
 * sizeof on 64-bit: 24 bytes (4-byte padding before int64_t).
 * sizeof on 32-bit: 20 bytes.
 */
struct env_sensor_data {
	uint32_t sensor_uid;   /**< Unique sensor identifier (Kconfig-assigned) */
	enum sensor_type type; /**< Physical quantity (enum is 32-bit)     */
	int32_t q31_value;     /**< Q31 fixed-point encoded measurement    */
	int64_t timestamp_ms;  /**< k_uptime_get() at sample time, ms      */
};

/** Broadcast UID — all hardware sensors respond to this trigger. */
#define HW_SENSOR_BROADCAST_UID 0xFFFFFFFFU

/** zbus channel carrying env_sensor_data events (defined in sensor_event.c). */
ZBUS_CHAN_DECLARE(sensor_event_chan);

/**
 * @brief Per-type descriptor: SI unit and Q31 decode function.
 *
 * Consumers that need to convert a raw q31_value to a physical double, or
 * look up the display unit, should use sensor_type_get_desc() rather than
 * maintaining their own switch statements.
 */
struct sensor_type_desc {
	/** SI unit string (UTF-8). Empty string for dimensionless quantities. */
	const char *unit;
	/**
	 * Decode a Q31-encoded measurement to a physical double.
	 * The encoding range is type-specific (see Q31 conversion helpers below).
	 */
	double (*decode_q31)(int32_t q31);
};

/**
 * @brief Look up the descriptor for a sensor type.
 *
 * @param t Sensor type.
 * @return Pointer to a static descriptor. Never NULL.
 */
const struct sensor_type_desc *sensor_type_get_desc(enum sensor_type t);

/**
 * @brief Map a sensor_type to its SI unit string (UTF-8).
 *
 * Convenience wrapper around sensor_type_get_desc(t)->unit.
 *
 * @return String literal, never NULL (empty string for dimensionless types).
 */
const char *sensor_type_to_unit(enum sensor_type t);

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

/*
 * CO₂: phys ∈ [0, 5000] ppm, span = 5000 ppm
 *   encode: q31 = co2_ppm / 5000.0 * INT32_MAX
 *   decode: co2_ppm = (double)q31 / INT32_MAX * 5000.0
 */

/**
 * @brief Encode a CO₂ concentration in ppm to Q31.
 * @param co2_ppm CO₂ in ppm (range 0 .. 5000).
 * @return Q31 encoded value.
 */
static inline int32_t co2_ppm_to_q31(double co2_ppm)
{
	if (!(co2_ppm >= 0.0)) {
		return 0;
	}
	if (co2_ppm >= 5000.0) {
		return INT32_MAX;
	}
	return (int32_t)(co2_ppm / 5000.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to CO₂ concentration in ppm.
 * @param q31 Q31 encoded CO₂.
 * @return CO₂ in ppm.
 */
static inline double q31_to_co2_ppm(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 5000.0;
}

/*
 * VOC air quality index: phys ∈ [0, 500] (Bosch IAQ scale), span = 500
 *   0–50   Excellent   50–100  Good   100–150  Moderate
 *   150–200 Unhealthy for sensitive groups   200–300 Unhealthy
 *   300–500 Very Unhealthy / Hazardous
 *   encode: q31 = iaq / 500.0 * INT32_MAX
 *   decode: iaq = (double)q31 / INT32_MAX * 500.0
 */

/**
 * @brief Encode a VOC air quality index to Q31.
 * @param iaq VOC index (range 0 .. 500).
 * @return Q31 encoded value.
 */
static inline int32_t voc_iaq_to_q31(double iaq)
{
	if (!(iaq >= 0.0)) {
		return 0;
	}
	if (iaq >= 500.0) {
		return INT32_MAX;
	}
	return (int32_t)(iaq / 500.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to VOC air quality index.
 * @param q31 Q31 encoded VOC index.
 * @return VOC index (0 .. 500).
 */
static inline double q31_to_voc_iaq(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 500.0;
}

/*
 * Pressure: phys ∈ [300, 1100] hPa, span = 800 hPa
 *   encode: q31 = (hpa - 300.0) / 800.0 * INT32_MAX
 *   decode: hpa = (double)q31 / INT32_MAX * 800.0 + 300.0
 */

/**
 * @brief Encode atmospheric pressure in hPa to Q31.
 * @param hpa Pressure in hPa (range 300 .. 1100).
 * @return Q31 encoded value.
 */
static inline int32_t pressure_hpa_to_q31(double hpa)
{
	if (hpa <= 300.0) {
		return INT32_MIN;
	}
	if (hpa >= 1100.0) {
		return INT32_MAX;
	}
	return (int32_t)((hpa - 300.0) / 800.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to atmospheric pressure in hPa.
 * @param q31 Q31 encoded pressure.
 * @return Pressure in hPa.
 */
static inline double q31_to_pressure_hpa(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 800.0 + 300.0;
}

/*
 * Gas resistance: phys ∈ [1kΩ, 10MΩ], log-scale
 *   encode: q31 = (log10(ohms) - 3.0) / 7.0 * INT32_MAX
 *   decode: ohms = pow(10.0, (double)q31 / INT32_MAX * 7.0 + 3.0)
 */

/**
 * @brief Encode gas resistance in ohms to Q31 (log-scale).
 * @param ohms Resistance in ohms (range 1000 .. 10000000).
 * @return Q31 encoded value.
 */
static inline int32_t gas_resistance_ohm_to_q31(double ohms)
{
	if (ohms <= 1000.0) {
		return INT32_MIN;
	}
	if (ohms >= 10000000.0) {
		return INT32_MAX;
	}
	return (int32_t)((log10(ohms) - 3.0) / 7.0 * (double)INT32_MAX);
}

/**
 * @brief Decode a Q31 value to gas resistance in ohms.
 * @param q31 Q31 encoded gas resistance.
 * @return Resistance in ohms.
 */
static inline double q31_to_gas_resistance_ohm(int32_t q31)
{
	return pow(10.0, (double)q31 / (double)INT32_MAX * 7.0 + 3.0);
}

/*
 * PM concentration: phys ∈ [0, 1000] µg/m³, span = 1000 µg/m³
 *   encode: q31 = (ugm3 / 1000.0) * INT32_MAX
 *   decode: ugm3 = (double)q31 / INT32_MAX * 1000.0
 */

static inline int32_t pm_ugm3_to_q31(double ugm3)
{
	if (!(ugm3 >= 0.0)) {
		return 0;
	}
	if (ugm3 >= 1000.0) {
		return INT32_MAX;
	}
	return (int32_t)(ugm3 / 1000.0 * (double)INT32_MAX);
}

static inline double q31_to_pm_ugm3(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * 1000.0;
}

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_EVENT_SENSOR_EVENT_H_ */
