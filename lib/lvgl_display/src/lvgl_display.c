/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lvgl_display.c
 * @brief LVGL graphical weather-station display (320x240 SDL window).
 *
 * UI layout:
 *   Line 0 (y=  4): "WEATHER STATION" title — Montserrat 14, cyan, centered
 *   Line 1 (y= 30): HH:MM clock           — Montserrat 48, white, centered
 *   Divider (y=100): horizontal line
 *   Lines 2+ (y=112+16*i): sensor rows     — Montserrat 14, default color
 *     "0x0001 TEMP  21.3 C"
 *     "0x0001 HUM   55.0 %"
 *     ...up to MAX_SENSOR_SLOTS rows
 *
 * Thread-safety model:
 *   - SYS_INIT at APPLICATION priority 91 creates all widgets (LVGL workqueue
 *     is ready at priority 90).
 *   - Widget creation is wrapped in lvgl_lock() / lvgl_unlock().
 *   - zbus listener callback (zbus thread): updates sensor_slots[] under
 *     k_spinlock, copies a snapshot, then calls lv_async_call().
 *   - lv_async_call callbacks run in the LVGL workqueue thread — safe to
 *     call lv_label_set_text() directly.
 *   - Clock uses k_work_delayable, fires every 60 s, uses lv_async_call().
 */

#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>
#include <zephyr/zbus/zbus.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(lvgl_display, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * Sensor slot storage
 * -------------------------------------------------------------------------- */

#define MAX_SENSOR_SLOTS 8

struct sensor_slot {
	bool valid;
	uint32_t uid;
	enum sensor_type type;
	int32_t q31_value;
};

static struct sensor_slot sensor_slots[MAX_SENSOR_SLOTS];
static struct k_spinlock slots_lock;

/* Snapshot passed to lv_async_call via a static buffer (single-producer). */
static struct sensor_slot slots_snapshot[MAX_SENSOR_SLOTS];

/* --------------------------------------------------------------------------
 * LVGL widgets
 * -------------------------------------------------------------------------- */

static lv_obj_t *clock_label;
static lv_obj_t *sensor_labels[MAX_SENSOR_SLOTS];
static char clock_text_buf[8]; /* "HH:MM\0" */

/* --------------------------------------------------------------------------
 * Clock async update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void clock_async_update(void *unused)
{
	ARG_UNUSED(unused);
	lv_label_set_text(clock_label, clock_text_buf);
}

static struct k_work_delayable clock_work;

static void clock_tick(struct k_work *work)
{
	struct timespec ts;

	sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	int64_t sod = ts.tv_sec % 86400;
	int hour = (int)(sod / 3600);
	int min = (int)((sod % 3600) / 60);

	snprintf(clock_text_buf, sizeof(clock_text_buf), "%02d:%02d", hour, min);
	lv_async_call(clock_async_update, NULL);

	k_work_reschedule(&clock_work, K_SECONDS(60));
}

/* --------------------------------------------------------------------------
 * Sensor async update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void sensor_async_update(void *unused)
{
	ARG_UNUSED(unused);

	for (int i = 0; i < MAX_SENSOR_SLOTS; i++) {
		if (!slots_snapshot[i].valid) {
			lv_label_set_text(sensor_labels[i], "");
			continue;
		}

		char buf[40];
		uint32_t uid = slots_snapshot[i].uid;
		int32_t q31 = slots_snapshot[i].q31_value;

		switch (slots_snapshot[i].type) {
		case SENSOR_TYPE_TEMPERATURE:
			snprintf(buf, sizeof(buf), "0x%04x TEMP  %.1f C", uid,
				 q31_to_temperature_c(q31));
			break;
		case SENSOR_TYPE_HUMIDITY:
			snprintf(buf, sizeof(buf), "0x%04x HUM   %.1f %%", uid,
				 q31_to_humidity_pct(q31));
			break;
		default:
			snprintf(buf, sizeof(buf), "0x%04x type%d 0x%08x", uid,
				 (int)slots_snapshot[i].type, (unsigned)q31);
			break;
		}

		lv_label_set_text(sensor_labels[i], buf);
	}
}

/* --------------------------------------------------------------------------
 * zbus listener (runs in zbus thread)
 * -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	k_spinlock_key_t key = k_spin_lock(&slots_lock);

	/* Find an existing slot for this uid+type, or claim an empty one. */
	int target = -1;

	for (int i = 0; i < MAX_SENSOR_SLOTS; i++) {
		if (sensor_slots[i].valid && sensor_slots[i].uid == evt->sensor_uid &&
		    sensor_slots[i].type == evt->type) {
			target = i;
			break;
		}
	}

	if (target < 0) {
		for (int i = 0; i < MAX_SENSOR_SLOTS; i++) {
			if (!sensor_slots[i].valid) {
				target = i;
				break;
			}
		}
	}

	if (target >= 0) {
		sensor_slots[target].valid = true;
		sensor_slots[target].uid = evt->sensor_uid;
		sensor_slots[target].type = evt->type;
		sensor_slots[target].q31_value = evt->q31_value;
	}

	/* Copy snapshot while holding spinlock. */
	memcpy(slots_snapshot, sensor_slots, sizeof(sensor_slots));

	k_spin_unlock(&slots_lock, key);

	lv_async_call(sensor_async_update, NULL);
}

ZBUS_LISTENER_DEFINE(lvgl_display_listener, sensor_event_cb);

/* --------------------------------------------------------------------------
 * UI creation
 * -------------------------------------------------------------------------- */

static void create_ui(void)
{
	lv_obj_t *screen = lv_screen_active();

	/* Title label */
	lv_obj_t *title = lv_label_create(screen);
	lv_label_set_text(title, "WEATHER STATION");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_style_text_color(title, lv_color_hex(0x00FFFF), LV_PART_MAIN);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

	/* Clock label */
	clock_label = lv_label_create(screen);
	lv_label_set_text(clock_label, "--:--");
	lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_48, LV_PART_MAIN);
	lv_obj_set_style_text_color(clock_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 30);

	/* Horizontal divider */
	lv_obj_t *line_obj = lv_obj_create(screen);
	lv_obj_set_size(line_obj, 300, 1);
	lv_obj_align(line_obj, LV_ALIGN_TOP_MID, 0, 100);
	lv_obj_set_style_bg_color(line_obj, lv_color_hex(0x888888), LV_PART_MAIN);
	lv_obj_set_style_border_width(line_obj, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(line_obj, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(line_obj, 0, LV_PART_MAIN);

	/* Sensor rows */
	for (int i = 0; i < MAX_SENSOR_SLOTS; i++) {
		sensor_labels[i] = lv_label_create(screen);
		lv_label_set_text(sensor_labels[i], "");
		lv_obj_set_style_text_font(sensor_labels[i], &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_pos(sensor_labels[i], 10, 112 + 16 * i);
	}
}

/* --------------------------------------------------------------------------
 * SYS_INIT
 * -------------------------------------------------------------------------- */

static int lvgl_display_init(void)
{
	lvgl_lock();
	create_ui();
	lvgl_unlock();

	k_work_init_delayable(&clock_work, clock_tick);
	k_work_reschedule(&clock_work, K_NO_WAIT);

	int rc = zbus_chan_add_obs(&sensor_event_chan, &lvgl_display_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to subscribe to sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("lvgl_display: init done");
	return 0;
}

SYS_INIT(lvgl_display_init, APPLICATION, 91);
