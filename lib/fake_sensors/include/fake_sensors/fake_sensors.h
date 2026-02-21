/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_sensors.h
 * @brief Public API for fake sensor drivers (temperature + humidity).
 *
 * Fake sensors are production-quality modules activated by
 * CONFIG_FAKE_SENSORS=y. Each DT node with compatible "fake,temperature"
 * or "fake,humidity" instantiates an independent driver that:
 *
 *   - subscribes to sensor_trigger_chan as a zbus listener
 *   - publishes env_sensor_data on sensor_event_chan when triggered
 *   - self-registers in sensor_registry during SYS_INIT (APPLICATION)
 *   - exports a fake_sensor_entry for shell introspection
 *
 * The shell sub-commands (fake_sensors list/temperature_set/humidity_set)
 * iterate STRUCT_SECTION_FOREACH(fake_sensor_entry, ...) to enumerate and
 * mutate live sensor values.
 */

#ifndef FAKE_SENSORS_FAKE_SENSORS_H_
#define FAKE_SENSORS_FAKE_SENSORS_H_

#include <stdint.h>
#include <zephyr/sys/iterable_sections.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sensor kind stored in fake_sensor_entry. */
enum fake_sensor_kind {
	FAKE_SENSOR_KIND_TEMPERATURE, /**< Value stored as milli-°C    */
	FAKE_SENSOR_KIND_HUMIDITY,    /**< Value stored as milli-%RH   */
};

/**
 * @brief Runtime descriptor for one fake sensor instance.
 *
 * Placed in the iterable linker section "fake_sensor_entry" via
 * STRUCT_SECTION_ITERABLE so the shell can enumerate all instances
 * without a central registry.
 */
struct fake_sensor_entry {
	uint32_t uid;               /**< Matches DT sensor-uid property  */
	enum fake_sensor_kind kind; /**< Temperature or humidity         */
	const char *label;          /**< Human-readable name             */
	const char *location;       /**< Physical location from DT       */
	int32_t *value_milli;       /**< Pointer to live milli-value     */
	/** Publish one sample immediately (called by shell set commands). */
	void (*publish)(struct fake_sensor_entry *entry);
};

#ifdef __cplusplus
}
#endif

#endif /* FAKE_SENSORS_FAKE_SENSORS_H_ */
