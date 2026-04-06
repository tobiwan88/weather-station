/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher_format.h
 * @brief Internal format helpers for mqtt_publisher.
 *
 * Pure functions — no dependency on sensor_registry, sntp_sync, or MQTT.
 * Exposed as non-static so they can be unit-tested from tests/mqtt_publisher.
 */

#ifndef MQTT_PUBLISHER_FORMAT_H_
#define MQTT_PUBLISHER_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

#include <sensor_event/sensor_event.h>

/**
 * @brief Map a sensor_type to a lowercase topic segment string.
 *
 * @return String literal, never NULL.
 */
const char *mqtt_publisher_type_to_topic_str(enum sensor_type t);

/**
 * @brief Map a sensor_type to its SI unit string (UTF-8).
 *
 * @return String literal, never NULL (empty string for dimensionless types).
 */
const char *mqtt_publisher_type_to_unit(enum sensor_type t);

/**
 * @brief Convert a Q31 value to a physical double for a given sensor type.
 *
 * Uses the correct per-type Q31 range.  Falls back to a normalised signed
 * Q31 conversion (approximately -1.0 .. +1.0) for unknown types.
 */
double mqtt_publisher_q31_to_value(enum sensor_type t, int32_t q31);

/**
 * @brief Build the MQTT topic string into @p buf.
 *
 * Format: {gateway}/{location}/{display_name}/{type}
 *
 * If @p location is NULL or empty, "unknown" is used.
 * If @p display_name is NULL, "unknown" is used.
 *
 * @param gateway      Gateway name (first segment).
 * @param location     Location string (may be NULL or empty).
 * @param display_name Sensor display name (may be NULL).
 * @param type         Sensor type enum value.
 * @param buf          Output buffer.
 * @param len          Output buffer length.
 */
void mqtt_publisher_build_topic(const char *gateway, const char *location, const char *display_name,
				enum sensor_type type, char *buf, size_t len);

/**
 * @brief Build the JSON payload string into @p buf.
 *
 * Format: {"time":<epoch_s>,"value":<float>,"unit":"<unit>"}
 *
 * @param epoch_s   Current time as Unix epoch seconds.
 * @param type      Sensor type (determines unit and Q31 decode).
 * @param q31_value Q31-encoded measurement.
 * @param buf       Output buffer.
 * @param len       Output buffer length.
 * @return          Number of bytes written (snprintf semantics), or <= 0 on error.
 */
int mqtt_publisher_build_payload(int64_t epoch_s, enum sensor_type type, int32_t q31_value,
				 char *buf, size_t len);

#endif /* MQTT_PUBLISHER_FORMAT_H_ */
