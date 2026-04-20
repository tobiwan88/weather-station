/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HTTP_DASHBOARD_JSON_SERIALISE_H_
#define HTTP_DASHBOARD_JSON_SERIALISE_H_

#include <stddef.h>
#include <stdint.h>

#include <sensor_event/sensor_event.h>

#include "sensor_history.h"

/**
 * @brief Map a sensor_type enum value to its JSON string name.
 *
 * Returns a string literal — always non-NULL.
 */
const char *sensor_type_str(enum sensor_type t);

/**
 * @brief Serialise a sensor-history snapshot to the /api/data JSON format.
 *
 * Iterates @p n_sensors slots of @p snap, skips invalid or empty ones (and
 * disabled sensors when CONFIG_SENSOR_REGISTRY_USER_META is enabled), and
 * emits a JSON object {"sensors":[...]} into @p buf.
 *
 * @param snap      Snapshot array (length @p n_sensors).
 * @param n_sensors Number of elements in @p snap.
 * @param buf       Output buffer.
 * @param buf_size  Size of @p buf in bytes.
 * @return Number of bytes written (without NUL terminator), or 0 on failure.
 */
size_t history_to_json(const struct sensor_history *snap, int n_sensors, uint8_t *buf,
		       size_t buf_size);

/**
 * @brief Serialise runtime config to the GET /api/config JSON format.
 *
 * Emits {"port":...,"trigger_interval_ms":...,"sntp_server":...,"locations":[...],
 * "sensors":[...],"mqtt":{...}} into @p buf.
 *
 * @param port         HTTP server port.
 * @param trigger_ms   Current trigger interval in milliseconds.
 * @param sntp_server  Current SNTP server string. Must not be NULL.
 * @param mqtt_cfg     MQTT config snapshot (NULL if MQTT not enabled).
 * @param buf          Output buffer. Must not be NULL.
 * @param buf_size     Size of @p buf in bytes.
 * @return Number of bytes written (without NUL terminator), or 0 on failure/overflow.
 */
size_t config_to_json(uint16_t port, uint32_t trigger_ms, const char *sntp_server,
		      const void *mqtt_cfg, uint8_t *buf, size_t buf_size);

/**
 * @brief Serialise the location registry to the GET /api/locations JSON format.
 *
 * Emits {"locations":[...]} into @p buf.
 *
 * @param buf       Output buffer.
 * @param buf_size  Size of @p buf in bytes.
 * @return Number of bytes written (without NUL terminator), or 0 on failure.
 */
size_t locations_to_json(uint8_t *buf, size_t buf_size);

#endif /* HTTP_DASHBOARD_JSON_SERIALISE_H_ */
