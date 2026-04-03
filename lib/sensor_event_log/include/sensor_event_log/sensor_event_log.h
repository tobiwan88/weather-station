/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sensor_event_log.h
 * @brief Sensor event console logger — no public API.
 *
 * Enable with CONFIG_SENSOR_EVENT_LOG=y.  The library self-registers a
 * zbus listener on sensor_event_chan via SYS_INIT and logs every event
 * to the console in human-readable form:
 *
 *   [00:01:23.456]  0x1234  TEMP   23.45 °C
 *   [00:01:23.456]  0x1234  HUM    65.2 %RH
 */

#ifndef SENSOR_EVENT_LOG_SENSOR_EVENT_LOG_H_
#define SENSOR_EVENT_LOG_SENSOR_EVENT_LOG_H_

/* No public API — library registers itself via SYS_INIT. */

#endif /* SENSOR_EVENT_LOG_SENSOR_EVENT_LOG_H_ */
