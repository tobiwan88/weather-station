/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lvgl_display.c
 * @brief LVGL graphical weather-station display (320x240 SDL window).
 *
 * UI layout (320 × 240 px):
 *
 *   +────────[x=160]────────────────────────[x=319]+
 *   │  analog clock   ┆  [□] 21.0°C               │  y=0
 *   │  (left panel)   ┆       55.0%                │
 *   │                 ├────────────────────────────│  y=48
 *   │                 ┆  [□] 19.5°C               │
 *   │                 ┆       62.0%                │
 *   │                 ├────────────────────────────│  y=96
 *   │                 ┆  [□] --°C                  │
 *   │                 ┆       --%                  │
 *   │                 ├────────────────────────────│  y=144
 *   │                 ┆  [□] --°C                  │
 *   │                 ┆       --%                  │
 *   +────────────────────────────────────────── ───+  y=193
 *   │  <-    MENU    Refresh    ->                  │  y=195 (27px)
 *   +───────┬────────┬────────┬─────────────── ────+  y=222
 *   │  B1   │   B2   │   B3   │   B4               │  y=222 (18px)
 *   +───────┴────────┴────────┴────────────────────+  y=240
 *
 * Thread-safety model:
 *   - SYS_INIT at APPLICATION priority 91 subscribes to sensor_event_chan.
 *   - lvgl_display_run() (called from main) creates widgets inside lvgl_lock,
 *     then drives lv_timer_handler in a loop.
 *   - zbus listener (zbus thread): updates sensor_groups[] under k_spinlock,
 *     copies snapshot, then calls lv_async_call().
 *   - lv_async_call callbacks run in the LVGL workqueue thread — safe to
 *     call any lv_*() function directly.
 *   - Clock uses k_work_delayable, fires every 60 s, uses lv_async_call().
 */

#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <math.h>

#ifndef M_PI
#	define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <time.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>
#include <zephyr/zbus/zbus.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(lvgl_display, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * Layout constants
 * -------------------------------------------------------------------------- */

#define CLOCK_CX     80 /* left panel center x */
#define CLOCK_CY     96 /* left panel center y */
#define CLOCK_FACE_R 65 /* face radius px */
#define CLOCK_HOUR_R 35 /* hour hand length */
#define CLOCK_MIN_R  52 /* minute hand length */
#define CLOCK_TICK_R 57 /* tick mark radius */

#define MAX_SENSOR_GROUPS 4

/* --------------------------------------------------------------------------
 * Sensor group storage (keyed by uid)
 * -------------------------------------------------------------------------- */

struct sensor_group {
	bool valid;
	uint32_t uid;
	bool has_temp;
	int32_t temp_q31;
	bool has_hum;
	int32_t hum_q31;
};

static struct sensor_group sensor_groups[MAX_SENSOR_GROUPS];
static struct sensor_group groups_snapshot[MAX_SENSOR_GROUPS];
static struct k_spinlock groups_lock;

/* --------------------------------------------------------------------------
 * LVGL widgets
 * -------------------------------------------------------------------------- */

/* Analog clock */
static lv_obj_t *hour_hand;
static lv_obj_t *min_hand;
static lv_point_precise_t hour_pts[2];
static lv_point_precise_t min_pts[2];
static int clock_hour;
static int clock_min;

/* Sensor groups */
static lv_obj_t *group_icon[MAX_SENSOR_GROUPS];
static lv_obj_t *group_temp_label[MAX_SENSOR_GROUPS];
static lv_obj_t *group_hum_label[MAX_SENSOR_GROUPS];

/* Nav bar */
static lv_obj_t *nav_label[4];

/* Button row */
static lv_obj_t *btn_box[4];

/* --------------------------------------------------------------------------
 * Analog clock helpers
 * -------------------------------------------------------------------------- */

static void update_clock_hands(int hour, int min)
{
	float h_angle = ((hour % 12) * 30.f + min * 0.5f) * (float)M_PI / 180.f - (float)M_PI / 2.f;
	float m_angle = (min * 6.f) * (float)M_PI / 180.f - (float)M_PI / 2.f;

	hour_pts[0].x = CLOCK_CX;
	hour_pts[0].y = CLOCK_CY;
	hour_pts[1].x = CLOCK_CX + (int)(CLOCK_HOUR_R * cosf(h_angle));
	hour_pts[1].y = CLOCK_CY + (int)(CLOCK_HOUR_R * sinf(h_angle));

	min_pts[0].x = CLOCK_CX;
	min_pts[0].y = CLOCK_CY;
	min_pts[1].x = CLOCK_CX + (int)(CLOCK_MIN_R * cosf(m_angle));
	min_pts[1].y = CLOCK_CY + (int)(CLOCK_MIN_R * sinf(m_angle));

	lv_line_set_points(hour_hand, hour_pts, 2);
	lv_line_set_points(min_hand, min_pts, 2);
}

/* --------------------------------------------------------------------------
 * Clock async update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void clock_async_update(void *unused)
{
	ARG_UNUSED(unused);
	if (!hour_hand || !min_hand) {
		return;
	}
	update_clock_hands(clock_hour, clock_min);
}

static struct k_work_delayable clock_work;

static void clock_tick(struct k_work *work)
{
	struct timespec ts;

	sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	int64_t sod = ts.tv_sec % 86400;

	clock_hour = (int)(sod / 3600);
	clock_min = (int)((sod % 3600) / 60);

	lv_async_call(clock_async_update, NULL);
	k_work_reschedule(&clock_work, K_SECONDS(60));
}

/* --------------------------------------------------------------------------
 * Sensor async update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void sensor_async_update(void *unused)
{
	ARG_UNUSED(unused);
	if (!group_temp_label[0]) {
		return;
	}

	for (int i = 0; i < MAX_SENSOR_GROUPS; i++) {
		char buf[16];

		if (!groups_snapshot[i].valid) {
			lv_label_set_text(group_temp_label[i], "--\xc2\xb0"
							       "C");
			lv_label_set_text(group_hum_label[i], "--%");
			continue;
		}

		if (groups_snapshot[i].has_temp) {
			snprintf(buf, sizeof(buf),
				 "%.1f\xc2\xb0"
				 "C",
				 q31_to_temperature_c(groups_snapshot[i].temp_q31));
		} else {
			snprintf(buf, sizeof(buf),
				 "--\xc2\xb0"
				 "C");
		}
		lv_label_set_text(group_temp_label[i], buf);

		if (groups_snapshot[i].has_hum) {
			snprintf(buf, sizeof(buf), "%.1f%%",
				 q31_to_humidity_pct(groups_snapshot[i].hum_q31));
		} else {
			snprintf(buf, sizeof(buf), "--%%");
		}
		lv_label_set_text(group_hum_label[i], buf);
	}
}

/* --------------------------------------------------------------------------
 * zbus listener (runs in zbus thread)
 * -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	k_spinlock_key_t key = k_spin_lock(&groups_lock);

	/* Find existing group for this uid, or claim an empty slot. */
	int target = -1;

	for (int i = 0; i < MAX_SENSOR_GROUPS; i++) {
		if (sensor_groups[i].valid && sensor_groups[i].uid == evt->sensor_uid) {
			target = i;
			break;
		}
	}

	if (target < 0) {
		for (int i = 0; i < MAX_SENSOR_GROUPS; i++) {
			if (!sensor_groups[i].valid) {
				target = i;
				break;
			}
		}
	}

	if (target >= 0) {
		sensor_groups[target].valid = true;
		sensor_groups[target].uid = evt->sensor_uid;

		if (evt->type == SENSOR_TYPE_TEMPERATURE) {
			sensor_groups[target].has_temp = true;
			sensor_groups[target].temp_q31 = evt->q31_value;
		} else if (evt->type == SENSOR_TYPE_HUMIDITY) {
			sensor_groups[target].has_hum = true;
			sensor_groups[target].hum_q31 = evt->q31_value;
		}
	}

	memcpy(groups_snapshot, sensor_groups, sizeof(sensor_groups));
	k_spin_unlock(&groups_lock, key);

	lv_async_call(sensor_async_update, NULL);
}

ZBUS_LISTENER_DEFINE(lvgl_display_listener, sensor_event_cb);

/* --------------------------------------------------------------------------
 * UI creation
 * -------------------------------------------------------------------------- */

static void create_ui(void)
{
	lv_obj_t *screen = lv_screen_active();

	/* Dark background for the whole screen */
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x0D1117), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

	/* --- Vertical divider (x=158, full main area height) --- */
	lv_obj_t *vdiv = lv_obj_create(screen);

	lv_obj_set_size(vdiv, 2, 193);
	lv_obj_set_pos(vdiv, 158, 0);
	lv_obj_set_style_bg_color(vdiv, lv_color_hex(0x666666), LV_PART_MAIN);
	lv_obj_set_style_border_width(vdiv, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(vdiv, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(vdiv, 0, LV_PART_MAIN);

	/* --- Analog clock face (left panel: x=0..157, y=0..192) --- */
	lv_obj_t *clock_face = lv_obj_create(screen);

	lv_obj_set_size(clock_face, 130, 130);
	lv_obj_set_pos(clock_face, 15, 31);
	lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(clock_face, lv_color_hex(0x111111), LV_PART_MAIN);
	lv_obj_set_style_border_width(clock_face, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(clock_face, lv_color_hex(0x666666), LV_PART_MAIN);
	lv_obj_set_style_pad_all(clock_face, 0, LV_PART_MAIN);

	/* 12 tick marks at r=CLOCK_TICK_R */
	for (int h = 0; h < 12; h++) {
		float angle = (h * 30.f - 90.f) * (float)M_PI / 180.f;
		int tx = CLOCK_CX + (int)(CLOCK_TICK_R * cosf(angle));
		int ty = CLOCK_CY + (int)(CLOCK_TICK_R * sinf(angle));

		lv_obj_t *tick = lv_obj_create(screen);

		lv_obj_set_size(tick, 4, 4);
		lv_obj_set_pos(tick, tx - 2, ty - 2);
		lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, LV_PART_MAIN);
		lv_obj_set_style_bg_color(tick, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
		lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
		lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
	}

	/* Hour hand */
	hour_hand = lv_line_create(screen);
	lv_obj_set_style_line_width(hour_hand, 4, LV_PART_MAIN);
	lv_obj_set_style_line_color(hour_hand, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_set_style_line_rounded(hour_hand, true, LV_PART_MAIN);

	/* Minute hand */
	min_hand = lv_line_create(screen);
	lv_obj_set_style_line_width(min_hand, 2, LV_PART_MAIN);
	lv_obj_set_style_line_color(min_hand, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
	lv_obj_set_style_line_rounded(min_hand, true, LV_PART_MAIN);

	/* Center cap */
	lv_obj_t *cap = lv_obj_create(screen);

	lv_obj_set_size(cap, 8, 8);
	lv_obj_set_pos(cap, CLOCK_CX - 4, CLOCK_CY - 4);
	lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(cap, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_set_style_border_width(cap, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(cap, 0, LV_PART_MAIN);

	/* Draw hands at 12:00 until first clock tick */
	update_clock_hands(0, 0);

	/* --- Sensor groups (right panel: x=160..319, y=0..192) --- */
	for (int i = 0; i < MAX_SENSOR_GROUPS; i++) {
		/* Icon placeholder */
		group_icon[i] = lv_obj_create(screen);
		lv_obj_set_size(group_icon[i], 16, 16);
		lv_obj_set_pos(group_icon[i], 162, i * 48 + 4);
		lv_obj_set_style_bg_color(group_icon[i], lv_color_hex(0x666666), LV_PART_MAIN);
		lv_obj_set_style_border_width(group_icon[i], 0, LV_PART_MAIN);
		lv_obj_set_style_radius(group_icon[i], 2, LV_PART_MAIN);
		lv_obj_set_style_pad_all(group_icon[i], 0, LV_PART_MAIN);

		/* Temperature label */
		group_temp_label[i] = lv_label_create(screen);
		lv_label_set_text(group_temp_label[i], "--\xc2\xb0"
						       "C");
		lv_obj_set_style_text_font(group_temp_label[i], &lv_font_montserrat_14,
					   LV_PART_MAIN);
		lv_obj_set_style_text_color(group_temp_label[i], lv_color_hex(0xE8D5B7),
					    LV_PART_MAIN);
		lv_obj_set_pos(group_temp_label[i], 182, i * 48 + 4);

		/* Humidity label */
		group_hum_label[i] = lv_label_create(screen);
		lv_label_set_text(group_hum_label[i], "--%");
		lv_obj_set_style_text_font(group_hum_label[i], &lv_font_montserrat_14,
					   LV_PART_MAIN);
		lv_obj_set_style_text_color(group_hum_label[i], lv_color_hex(0xAAAAAA),
					    LV_PART_MAIN);
		lv_obj_set_pos(group_hum_label[i], 182, i * 48 + 22);

		/* Divider between groups (not after last) */
		if (i < MAX_SENSOR_GROUPS - 1) {
			lv_obj_t *hdiv = lv_obj_create(screen);

			lv_obj_set_size(hdiv, 158, 1);
			lv_obj_set_pos(hdiv, 161, i * 48 + 47);
			lv_obj_set_style_bg_color(hdiv, lv_color_hex(0x444444), LV_PART_MAIN);
			lv_obj_set_style_border_width(hdiv, 0, LV_PART_MAIN);
			lv_obj_set_style_pad_all(hdiv, 0, LV_PART_MAIN);
			lv_obj_set_style_radius(hdiv, 0, LV_PART_MAIN);
		}
	}

	/* --- Horizontal separator (y=193) --- */
	lv_obj_t *hsep = lv_obj_create(screen);

	lv_obj_set_size(hsep, 320, 2);
	lv_obj_set_pos(hsep, 0, 193);
	lv_obj_set_style_bg_color(hsep, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_set_style_border_width(hsep, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(hsep, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(hsep, 0, LV_PART_MAIN);

	/* --- Nav bar background (y=195, h=27) --- */
	lv_obj_t *nav_bg = lv_obj_create(screen);

	lv_obj_set_size(nav_bg, 320, 27);
	lv_obj_set_pos(nav_bg, 0, 195);
	lv_obj_set_style_bg_color(nav_bg, lv_color_hex(0x222222), LV_PART_MAIN);
	lv_obj_set_style_border_width(nav_bg, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(nav_bg, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(nav_bg, 0, LV_PART_MAIN);

	/* Nav labels: 4 equal 80-px sections */
	static const char *const nav_texts[4] = {"<-", "MENU", "Refresh", "->"};

	for (int i = 0; i < 4; i++) {
		nav_label[i] = lv_label_create(screen);
		lv_label_set_text(nav_label[i], nav_texts[i]);
		lv_obj_set_style_text_font(nav_label[i], &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_style_text_color(nav_label[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
		lv_obj_set_style_text_align(nav_label[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
		lv_obj_set_size(nav_label[i], 80, 27);
		lv_obj_set_pos(nav_label[i], i * 80, 195);
	}

	/* --- Button row (y=222, h=18) --- */
	for (int i = 0; i < 4; i++) {
		btn_box[i] = lv_obj_create(screen);
		lv_obj_set_size(btn_box[i], 76, 16);
		lv_obj_set_pos(btn_box[i], 2 + i * 80, 223);
		lv_obj_set_style_bg_color(btn_box[i], lv_color_hex(0x333333), LV_PART_MAIN);
		lv_obj_set_style_border_width(btn_box[i], 1, LV_PART_MAIN);
		lv_obj_set_style_border_color(btn_box[i], lv_color_hex(0x666666), LV_PART_MAIN);
		lv_obj_set_style_radius(btn_box[i], 3, LV_PART_MAIN);
		lv_obj_set_style_pad_all(btn_box[i], 0, LV_PART_MAIN);
	}
}

/* --------------------------------------------------------------------------
 * Main-thread timer loop
 * -------------------------------------------------------------------------- */

void lvgl_display_run(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	lvgl_lock();
	create_ui();
	k_work_init_delayable(&clock_work, clock_tick);
	k_work_reschedule(&clock_work, K_NO_WAIT);
	lv_timer_handler();
	lvgl_unlock();

	display_blanking_off(display_dev);

	while (true) {
		uint32_t next_ms;

		lvgl_lock();
		next_ms = lv_timer_handler();
		lvgl_unlock();

		if (next_ms == LV_NO_TIMER_READY) {
			k_msleep(CONFIG_LV_DEF_REFR_PERIOD);
		} else {
			k_msleep(next_ms);
		}
	}
}

/* --------------------------------------------------------------------------
 * SYS_INIT
 * -------------------------------------------------------------------------- */

static int lvgl_display_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &lvgl_display_listener, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("Failed to subscribe to sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("lvgl_display: init done");
	return 0;
}

SYS_INIT(lvgl_display_init, APPLICATION, 91);
