/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HTTP_DASHBOARD_SENSOR_HISTORY_H_
#define HTTP_DASHBOARD_SENSOR_HISTORY_H_

#include <stdbool.h>
#include <stdint.h>

#include <sensor_event/sensor_event.h>

struct sensor_sample {
	int64_t timestamp_ms;
	int32_t q31_value;
};

struct sensor_history {
	bool valid;
	uint32_t uid;
	enum sensor_type type;
	struct sensor_sample samples[CONFIG_HTTP_DASHBOARD_HISTORY_SIZE];
	uint16_t head;  /* next write index */
	uint16_t count; /* number of valid samples (capped at HISTORY_SIZE) */
};

/**
 * @brief Push one sensor event into a caller-supplied history array.
 *
 * Pure logic — no spinlock, no global state. Finds an existing slot that
 * matches @p evt->sensor_uid and @p evt->type, or allocates a new one.
 * The sample is appended to the ring buffer and the head/count are updated.
 *
 * @param h_arr     Array of sensor_history slots (length @p max_slots).
 * @param max_slots Number of elements in @p h_arr.
 * @param evt       Event to record.
 * @return Index of the slot used, or -1 if the array is full.
 */
int history_push(struct sensor_history *h_arr, int max_slots, const struct env_sensor_data *evt);

/**
 * @brief Record an event into the module-internal history (thread-safe).
 *
 * Takes the module-internal spinlock, calls history_push on the internal
 * histories[] array, then releases the lock. Called from the zbus listener.
 */
void history_record_event(const struct env_sensor_data *evt);

/**
 * @brief Snapshot the module-internal history into the internal snap[] array.
 *
 * Takes the spinlock, memcpy-s histories[] → snap[], releases the lock.
 * Must be called before history_get_snap().
 */
void history_do_snapshot(void);

/**
 * @brief Return a read-only pointer to the most recent snapshot.
 *
 * Valid until the next call to history_do_snapshot().
 * Length is always CONFIG_HTTP_DASHBOARD_MAX_SENSORS.
 */
const struct sensor_history *history_get_snap(void);

#endif /* HTTP_DASHBOARD_SENSOR_HISTORY_H_ */
