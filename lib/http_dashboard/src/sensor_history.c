/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sensor_history.h"

LOG_MODULE_DECLARE(http_dashboard, CONFIG_HTTP_DASHBOARD_LOG_LEVEL);

static struct sensor_history histories[CONFIG_HTTP_DASHBOARD_MAX_SENSORS];
static struct sensor_history snap[CONFIG_HTTP_DASHBOARD_MAX_SENSORS];
static struct k_spinlock history_lock;

int history_push(struct sensor_history *h_arr, int max_slots, const struct env_sensor_data *evt)
{
	int slot = -1;

	for (int i = 0; i < max_slots; i++) {
		if (h_arr[i].valid && h_arr[i].uid == evt->sensor_uid &&
		    h_arr[i].type == evt->type) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		for (int i = 0; i < max_slots; i++) {
			if (!h_arr[i].valid) {
				slot = i;
				h_arr[i].valid = true;
				h_arr[i].uid = evt->sensor_uid;
				h_arr[i].type = evt->type;
				h_arr[i].head = 0;
				h_arr[i].count = 0;
				break;
			}
		}
	}

	if (slot < 0) {
		return -1;
	}

	struct sensor_history *h = &h_arr[slot];

	h->samples[h->head].timestamp_ms = evt->timestamp_ms;
	h->samples[h->head].q31_value = evt->q31_value;
	h->head = (h->head + 1) % CONFIG_HTTP_DASHBOARD_HISTORY_SIZE;
	if (h->count < CONFIG_HTTP_DASHBOARD_HISTORY_SIZE) {
		h->count++;
	}

	return slot;
}

void history_record_event(const struct env_sensor_data *evt)
{
	k_spinlock_key_t key = k_spin_lock(&history_lock);

	if (history_push(histories, CONFIG_HTTP_DASHBOARD_MAX_SENSORS, evt) < 0) {
		LOG_WRN("sensor history full, event dropped");
	}
	k_spin_unlock(&history_lock, key);
}

void history_do_snapshot(void)
{
	k_spinlock_key_t key = k_spin_lock(&history_lock);

	memcpy(snap, histories, sizeof(histories));
	k_spin_unlock(&history_lock, key);
}

const struct sensor_history *history_get_snap(void)
{
	return snap;
}
