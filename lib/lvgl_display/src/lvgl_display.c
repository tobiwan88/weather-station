/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lvgl_display.c
 * @brief LVGL graphical weather-station display (320x240 SDL window).
 *
 * UI layout (CONFIG_LVGL_DISPLAY_SHOW_CLOCK=y, default):
 *
 *   +────────[x=160]────────────────────────[x=319]+
 *   │  analog clock   ┆  card 0  (uid A, temp)     │  y=0
 *   │  (left panel)   ┆  card 1  (uid A, hum)      │
 *   │                 ┆  card 2  (uid B, co2)       │
 *   │                 ┆  card 3  (uid B, voc)       │
 *   +─────────────────────────────────────────── ───+  y=193
 *   │  [◄]  1/2   MENU   [►]                        │  y=195 (27px)
 *   +───────────────────────────────────────────────+  y=222
 *   │  B1    B2    B3    B4   (hidden by default)    │  y=222 (18px)
 *   +───────────────────────────────────────────────+  y=240
 *
 * Cards are created on demand — the first event for a (uid, type) pair
 * allocates a slot and builds the widget.  Flex layout positions cards
 * automatically.  The nav-bar arrows page through cards when there are
 * more than CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE active.
 *
 * Thread-safety model:
 *   - SYS_INIT at APPLICATION priority 91 subscribes to sensor_event_chan.
 *   - lvgl_display_run() (called from main) creates widgets inside lvgl_lock,
 *     then drives lv_timer_handler in a loop.
 *   - zbus listener (zbus thread): updates sensor_cards[] under k_spinlock,
 *     copies snapshot, then calls lv_async_call().
 *   - lv_async_call callbacks run in the LVGL workqueue thread — safe to
 *     call any lv_*() function directly.
 *   - Clock uses k_work_delayable, fires every 60 s, uses lv_async_call().
 *   - card_widgets[] and page_first_card are only accessed from LVGL
 *     workqueue / event callbacks — no lock needed.
 */

#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <math.h>

#ifndef M_PI
#	define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <string.h>
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

#if defined(CONFIG_LVGL_DISPLAY_USE_SENSOR_REGISTRY)
#	include <sensor_registry/sensor_registry.h>
#endif

#include "theme.h"

LOG_MODULE_REGISTER(lvgl_display, CONFIG_LVGL_DISPLAY_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Layout constants
 * -------------------------------------------------------------------------- */

#define CLOCK_CX     80 /* left panel center x */
#define CLOCK_CY     96 /* left panel center y */
#define CLOCK_FACE_R 65 /* face radius px */
#define CLOCK_HOUR_R 35 /* hour hand length */
#define CLOCK_MIN_R  52 /* minute hand length */
#define CLOCK_TICK_R 57 /* tick mark radius */

#if defined(CONFIG_LVGL_DISPLAY_SHOW_CLOCK)
#	define SENSOR_PANEL_X 160
#	define SENSOR_PANEL_W 160
#else
#	define SENSOR_PANEL_X 0
#	define SENSOR_PANEL_W 320
#endif

#define SENSOR_PANEL_H 193 /* height above the nav bar */

/* Card height: divide available space evenly across the configured page size */
#define CARD_HEIGHT                                                                                \
	((SENSOR_PANEL_H - (CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE + 1) * 2) /                       \
	 CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE)

#define CARD_WIDTH (SENSOR_PANEL_W - 4) /* 2px left + right container pad */

/* --------------------------------------------------------------------------
 * Sensor card data storage (spinlock-protected, keyed by uid + type)
 * -------------------------------------------------------------------------- */

struct sensor_card {
	bool valid;            /**< Slot occupied                           */
	uint32_t uid;          /**< Sensor uid from env_sensor_data         */
	enum sensor_type type; /**< One card = one physical quantity      */
	int32_t q31_value;     /**< Latest raw Q31 measurement              */
	bool has_value;        /**< False until first event arrives         */
};

/* Fallback max sensor name length when CONFIG_SENSOR_REGISTRY_META_NAME_LEN is not defined */
#ifndef CONFIG_SENSOR_REGISTRY_META_NAME_LEN
#	define CONFIG_SENSOR_REGISTRY_META_NAME_LEN 16
#endif

static struct sensor_card sensor_cards[CONFIG_LVGL_DISPLAY_MAX_CARDS];
static struct sensor_card cards_snapshot[CONFIG_LVGL_DISPLAY_MAX_CARDS];
static struct k_spinlock cards_lock;

/* --------------------------------------------------------------------------
 * LVGL widget handles (LVGL workqueue thread only — no lock needed)
 * -------------------------------------------------------------------------- */

struct sensor_card_widgets {
	lv_obj_t *card_obj;    /**< Flex-child card container           */
	lv_obj_t *icon_label;  /**< LV_SYMBOL_* colored accent          */
	lv_obj_t *name_label;  /**< Sensor name (registry or type name) */
	lv_obj_t *value_label; /**< Formatted numeric reading           */
	lv_obj_t *unit_label;  /**< SI unit string                      */
	lv_obj_t *gauge;       /**< lv_arc (GAUGE style only)           */
	lv_obj_t *bar;         /**< lv_bar (BAR style only)             */
};

static struct sensor_card_widgets card_widgets[CONFIG_LVGL_DISPLAY_MAX_CARDS];

/* Sensor card flex container (parent for all card objects) */
static lv_obj_t *sensor_container;

/* Paging state */
static int page_first_card;

/* Nav paging labels */
static lv_obj_t *nav_page_label; /**< "1/2" page indicator */

/* --------------------------------------------------------------------------
 * Analog clock
 * -------------------------------------------------------------------------- */

static lv_obj_t *hour_hand;
static lv_obj_t *min_hand;
static lv_point_precise_t hour_pts[2];
static lv_point_precise_t min_pts[2];
static int clock_hour;
static int clock_min;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static int count_valid_cards(void)
{
	int n = 0;

	for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
		if (cards_snapshot[i].valid) {
			n++;
		}
	}
	return n;
}

static void get_card_name(uint32_t uid, enum sensor_type type, char *buf, size_t len)
{
#if defined(CONFIG_LVGL_DISPLAY_USE_SENSOR_REGISTRY)
	const char *name = sensor_registry_get_display_name(uid);

	if (name && name[0] != '\0') {
		snprintf(buf, len, "%s", name);
		return;
	}
#else
	ARG_UNUSED(uid);
#endif
	static const char *const type_names[] = {
		"Temp", "Hum", "Press", "CO2", "VOC", "Light", "UV", "Batt",
	};

	if ((unsigned)type < ARRAY_SIZE(type_names)) {
		snprintf(buf, len, "%s", type_names[type]);
	} else {
		snprintf(buf, len, "Sensor");
	}
}

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
 * Paging
 * -------------------------------------------------------------------------- */

static void update_nav_indicators(void)
{
	if (!nav_page_label) {
		return;
	}

	int total = count_valid_cards();
	int pages = (total + CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE - 1) /
		    CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE;

	if (pages < 1) {
		pages = 1;
	}
	int current_page = page_first_card / CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE + 1;
	char buf[16];

	snprintf(buf, sizeof(buf), "%d/%d", current_page, pages);
	lv_label_set_text(nav_page_label, buf);
}

static void apply_paging(void)
{
	int slot = 0; /* index into card_widgets[] counting only created cards */

	for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
		if (!card_widgets[i].card_obj) {
			continue;
		}
		int page_pos = slot - page_first_card;

		if (page_pos >= 0 && page_pos < CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE) {
			lv_obj_remove_flag(card_widgets[i].card_obj, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(card_widgets[i].card_obj, LV_OBJ_FLAG_HIDDEN);
		}
		slot++;
	}
	update_nav_indicators();
}

static void nav_prev_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (page_first_card >= CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE) {
		page_first_card -= CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE;
		apply_paging();
	}
}

static void nav_next_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	int total = count_valid_cards();

	if (page_first_card + CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE < total) {
		page_first_card += CONFIG_LVGL_DISPLAY_SENSORS_PER_PAGE;
		apply_paging();
	}
}

/* --------------------------------------------------------------------------
 * Card creation (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void create_sensor_card(int slot, const struct sensor_card *cards_local)
{
	enum sensor_type t = cards_local[slot].type;
	uint32_t uid = cards_local[slot].uid;
	lv_color_t accent = theme_sensor_color(t);

	/* Card container — flex-row child of sensor_container */
	lv_obj_t *card = lv_obj_create(sensor_container);

	lv_obj_set_size(card, CARD_WIDTH, CARD_HEIGHT);
	lv_obj_add_style(card, &g_style_card, LV_PART_MAIN);
	/* Remove default scrollbar and border */
	lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

	card_widgets[slot].card_obj = card;

#if defined(CONFIG_LVGL_DISPLAY_CARD_STYLE_GAUGE)

	/*
	 * GAUGE layout:
	 *   Left section (flex-column): icon + name
	 *   Right section: lv_arc (50x50) with value label overlaid
	 */
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	/* Left info column */
	lv_obj_t *info_col = lv_obj_create(card);

	lv_obj_remove_style_all(info_col);
	lv_obj_set_size(info_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);

	lv_obj_t *icon = lv_label_create(info_col);

	lv_label_set_text(icon, theme_sensor_symbol(t));
	lv_obj_set_style_text_color(icon, accent, LV_PART_MAIN);
	lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);

	char name_buf[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];

	get_card_name(uid, t, name_buf, sizeof(name_buf));
	lv_obj_t *name = lv_label_create(info_col);

	lv_label_set_text(name, name_buf);
	lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(name, 60);
	lv_obj_add_style(name, &g_style_name_label, LV_PART_MAIN);

	card_widgets[slot].icon_label = icon;
	card_widgets[slot].name_label = name;

	/* Arc gauge */
	int arc_size = CARD_HEIGHT - 8;

	if (arc_size < 20) {
		arc_size = 20;
	}

	lv_obj_t *arc = lv_arc_create(card);

	lv_obj_set_size(arc, arc_size, arc_size);
	lv_arc_set_rotation(arc, 135);
	lv_arc_set_bg_angles(arc, 0, 270);
	lv_arc_set_range(arc, 0, 1000);
	lv_arc_set_value(arc, 0);
	lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
	lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
	lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A3040), LV_PART_MAIN);
	lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
	lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

	/* Value label floating over arc center */
	lv_obj_t *val = lv_label_create(arc);

	lv_label_set_text(val, "--");
	lv_obj_add_style(val, &g_style_value_label, LV_PART_MAIN);
	lv_obj_set_style_text_color(val, accent, LV_PART_MAIN);
	lv_obj_set_style_text_font(val, &lv_font_montserrat_10, LV_PART_MAIN);
	lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);

	card_widgets[slot].gauge = arc;
	card_widgets[slot].value_label = val;
	card_widgets[slot].unit_label = NULL;

#elif defined(CONFIG_LVGL_DISPLAY_CARD_STYLE_BAR)

	/*
	 * BAR layout (flex-column):
	 *   Row 1 (flex-row): icon + name + value + unit
	 *   Row 2: lv_bar spanning full card width
	 */
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);

	/* Top row */
	lv_obj_t *top_row = lv_obj_create(card);

	lv_obj_remove_style_all(top_row);
	lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(top_row, 3, LV_PART_MAIN);

	lv_obj_t *icon = lv_label_create(top_row);

	lv_label_set_text(icon, theme_sensor_symbol(t));
	lv_obj_set_style_text_color(icon, accent, LV_PART_MAIN);
	lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);

	char name_buf2[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];

	get_card_name(uid, t, name_buf2, sizeof(name_buf2));
	lv_obj_t *name = lv_label_create(top_row);

	lv_label_set_text(name, name_buf2);
	lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
	lv_obj_set_flex_grow(name, 1);
	lv_obj_add_style(name, &g_style_name_label, LV_PART_MAIN);

	lv_obj_t *val = lv_label_create(top_row);

	lv_label_set_text(val, "--");
	lv_obj_add_style(val, &g_style_value_label, LV_PART_MAIN);
	lv_obj_set_style_text_color(val, accent, LV_PART_MAIN);

	lv_obj_t *unit = lv_label_create(top_row);

	lv_label_set_text(unit, sensor_type_to_unit(t));
	lv_obj_add_style(unit, &g_style_unit_label, LV_PART_MAIN);

	/* Bar */
	lv_obj_t *bar = lv_bar_create(card);

	lv_obj_set_size(bar, LV_PCT(100), 5);
	lv_bar_set_range(bar, 0, 1000);
	lv_bar_set_value(bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A3040), LV_PART_MAIN);
	lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
	lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
	lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

	card_widgets[slot].icon_label = icon;
	card_widgets[slot].name_label = name;
	card_widgets[slot].value_label = val;
	card_widgets[slot].unit_label = unit;
	card_widgets[slot].bar = bar;

#else /* NUMERIC (default) */

	/*
	 * NUMERIC layout (flex-row):
	 *   icon | name (grow) | value | unit
	 */
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(card, 3, LV_PART_MAIN);

	lv_obj_t *icon = lv_label_create(card);

	lv_label_set_text(icon, theme_sensor_symbol(t));
	lv_obj_set_style_text_color(icon, accent, LV_PART_MAIN);
	lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);

	char name_buf3[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];

	get_card_name(uid, t, name_buf3, sizeof(name_buf3));
	lv_obj_t *name = lv_label_create(card);

	lv_label_set_text(name, name_buf3);
	lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
	lv_obj_set_flex_grow(name, 1);
	lv_obj_add_style(name, &g_style_name_label, LV_PART_MAIN);

	lv_obj_t *val = lv_label_create(card);

	lv_label_set_text(val, "--");
	lv_obj_add_style(val, &g_style_value_label, LV_PART_MAIN);
	lv_obj_set_style_text_color(val, accent, LV_PART_MAIN);

	lv_obj_t *unit = lv_label_create(card);

	lv_label_set_text(unit, sensor_type_to_unit(t));
	lv_obj_add_style(unit, &g_style_unit_label, LV_PART_MAIN);

	card_widgets[slot].icon_label = icon;
	card_widgets[slot].name_label = name;
	card_widgets[slot].value_label = val;
	card_widgets[slot].unit_label = unit;

#endif /* card style */
}

/* --------------------------------------------------------------------------
 * Card update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void update_sensor_card(int slot, const struct sensor_card *cards_local)
{
	struct sensor_card_widgets *w = &card_widgets[slot];

	if (!w->card_obj) {
		return;
	}

	enum sensor_type t = cards_local[slot].type;
	uint32_t uid = cards_local[slot].uid;

	/* Refresh sensor name (may have been updated via dashboard) */
	if (w->name_label) {
		char name_buf[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];

		get_card_name(uid, t, name_buf, sizeof(name_buf));
		lv_label_set_text(w->name_label, name_buf);
	}

	if (!cards_local[slot].has_value) {
		if (w->value_label) {
			lv_label_set_text(w->value_label, "--");
		}
		return;
	}

	const struct sensor_type_desc *desc = sensor_type_get_desc(t);
	double phys = desc->decode_q31(cards_local[slot].q31_value);

	/* Format value string */
	char val_buf[16];

	snprintf(val_buf, sizeof(val_buf), "%.1f", phys);

	if (w->value_label) {
		lv_label_set_text(w->value_label, val_buf);
	}

#if defined(CONFIG_LVGL_DISPLAY_CARD_STYLE_GAUGE)
	if (w->gauge) {
		lv_arc_set_value(w->gauge, theme_scale_value(t, phys));
	}
#elif defined(CONFIG_LVGL_DISPLAY_CARD_STYLE_BAR)
	if (w->bar) {
		lv_bar_set_value(w->bar, theme_scale_value(t, phys), LV_ANIM_OFF);
	}
#endif
}

/* --------------------------------------------------------------------------
 * Sensor async update (runs in LVGL workqueue thread)
 * -------------------------------------------------------------------------- */

static void sensor_async_update(void *unused)
{
	ARG_UNUSED(unused);

	if (!sensor_container) {
		return;
	}

	/* Snapshot cards_snapshot under spinlock to avoid data race with zbus listener */
	struct sensor_card cards_local[CONFIG_LVGL_DISPLAY_MAX_CARDS];
	k_spinlock_key_t key = k_spin_lock(&cards_lock);
	memcpy(cards_local, cards_snapshot, sizeof(cards_snapshot));
	k_spin_unlock(&cards_lock, key);

	/* Phase 1: create widgets for newly seen (uid, type) pairs */
	for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
		if (cards_local[i].valid && !card_widgets[i].card_obj) {
			create_sensor_card(i, cards_local);
		}
	}

	/* Phase 2: update all live cards */
	for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
		if (cards_local[i].valid && card_widgets[i].card_obj) {
			update_sensor_card(i, cards_local);
		}
	}

	/* Phase 3: refresh paging visibility */
	apply_paging();
}

/* --------------------------------------------------------------------------
 * zbus listener (runs in zbus thread)
 * -------------------------------------------------------------------------- */

static void sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	k_spinlock_key_t key = k_spin_lock(&cards_lock);

	/* Find existing slot for (uid, type), or claim an empty one. */
	int target = -1;

	for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
		if (sensor_cards[i].valid && sensor_cards[i].uid == evt->sensor_uid &&
		    sensor_cards[i].type == evt->type) {
			target = i;
			break;
		}
	}

	if (target < 0) {
		for (int i = 0; i < CONFIG_LVGL_DISPLAY_MAX_CARDS; i++) {
			if (!sensor_cards[i].valid) {
				target = i;
				break;
			}
		}
	}

	if (target >= 0) {
		sensor_cards[target].valid = true;
		sensor_cards[target].uid = evt->sensor_uid;
		sensor_cards[target].type = evt->type;
		sensor_cards[target].q31_value = evt->q31_value;
		sensor_cards[target].has_value = true;
	}

	memcpy(cards_snapshot, sensor_cards, sizeof(sensor_cards));
	k_spin_unlock(&cards_lock, key);

	lv_async_call(sensor_async_update, NULL);
}

ZBUS_LISTENER_DEFINE(lvgl_display_listener, sensor_event_cb);

/* --------------------------------------------------------------------------
 * UI creation
 * -------------------------------------------------------------------------- */

static void create_ui(void)
{
	theme_init();

	lv_obj_t *screen = lv_screen_active();

	/* Screen background */
	lv_obj_set_style_bg_color(screen, lv_color_hex(THEME_SCREEN_BG_COLOR), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

#if defined(CONFIG_LVGL_DISPLAY_SHOW_CLOCK)
	/* Vertical divider between clock and sensor panels */
	lv_obj_t *vdiv = lv_obj_create(screen);

	lv_obj_set_size(vdiv, 2, SENSOR_PANEL_H);
	lv_obj_set_pos(vdiv, SENSOR_PANEL_X - 2, 0);
	lv_obj_set_style_bg_color(vdiv, lv_color_hex(THEME_DIVIDER_COLOR), LV_PART_MAIN);
	lv_obj_set_style_border_width(vdiv, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(vdiv, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(vdiv, 0, LV_PART_MAIN);

	/* Analog clock face */
	lv_obj_t *clock_face = lv_obj_create(screen);

	lv_obj_set_size(clock_face, 130, 130);
	lv_obj_set_pos(clock_face, 15, 31);
	lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(clock_face, lv_color_hex(THEME_CLOCK_BG_COLOR), LV_PART_MAIN);
	lv_obj_set_style_border_width(clock_face, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(clock_face, lv_color_hex(THEME_CLOCK_BORDER_COLOR),
				      LV_PART_MAIN);
	lv_obj_set_style_pad_all(clock_face, 0, LV_PART_MAIN);

	/* 12 tick marks */
	for (int h = 0; h < 12; h++) {
		float angle = (h * 30.f - 90.f) * (float)M_PI / 180.f;
		int tx = CLOCK_CX + (int)(CLOCK_TICK_R * cosf(angle));
		int ty = CLOCK_CY + (int)(CLOCK_TICK_R * sinf(angle));

		lv_obj_t *tick = lv_obj_create(screen);

		lv_obj_set_size(tick, 4, 4);
		lv_obj_set_pos(tick, tx - 2, ty - 2);
		lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, LV_PART_MAIN);
		lv_obj_set_style_bg_color(tick, lv_color_hex(THEME_CLOCK_TICK_COLOR), LV_PART_MAIN);
		lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
		lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
	}

	/* Hour hand */
	hour_hand = lv_line_create(screen);
	lv_obj_set_style_line_width(hour_hand, THEME_CLOCK_HOUR_WIDTH, LV_PART_MAIN);
	lv_obj_set_style_line_color(hour_hand, lv_color_hex(THEME_CLOCK_HOUR_COLOR), LV_PART_MAIN);
	lv_obj_set_style_line_rounded(hour_hand, true, LV_PART_MAIN);

	/* Minute hand */
	min_hand = lv_line_create(screen);
	lv_obj_set_style_line_width(min_hand, THEME_CLOCK_MIN_WIDTH, LV_PART_MAIN);
	lv_obj_set_style_line_color(min_hand, lv_color_hex(THEME_CLOCK_MIN_COLOR), LV_PART_MAIN);
	lv_obj_set_style_line_rounded(min_hand, true, LV_PART_MAIN);

	/* Center cap */
	lv_obj_t *cap = lv_obj_create(screen);

	lv_obj_set_size(cap, THEME_CLOCK_CAP_SIZE, THEME_CLOCK_CAP_SIZE);
	lv_obj_set_pos(cap, CLOCK_CX - THEME_CLOCK_CAP_SIZE / 2,
		       CLOCK_CY - THEME_CLOCK_CAP_SIZE / 2);
	lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_bg_color(cap, lv_color_hex(THEME_CLOCK_CAP_COLOR), LV_PART_MAIN);
	lv_obj_set_style_border_width(cap, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(cap, 0, LV_PART_MAIN);

	/* Draw hands at 12:00 until first clock tick */
	update_clock_hands(0, 0);
#endif /* CONFIG_LVGL_DISPLAY_SHOW_CLOCK */

	/* --- Sensor card container (right panel, flex-column) --- */
	sensor_container = lv_obj_create(screen);
	lv_obj_set_pos(sensor_container, SENSOR_PANEL_X, 0);
	lv_obj_set_size(sensor_container, SENSOR_PANEL_W, SENSOR_PANEL_H);
	lv_obj_set_style_bg_opa(sensor_container, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(sensor_container, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(sensor_container, 2, LV_PART_MAIN);
	lv_obj_set_style_pad_row(sensor_container, 2, LV_PART_MAIN);
	lv_obj_set_style_radius(sensor_container, 0, LV_PART_MAIN);
	lv_obj_set_scroll_dir(sensor_container, LV_DIR_NONE);
	lv_obj_set_scrollbar_mode(sensor_container, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_flex_flow(sensor_container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(sensor_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);

	/* --- Horizontal separator --- */
	lv_obj_t *hsep = lv_obj_create(screen);

	lv_obj_set_size(hsep, 320, 2);
	lv_obj_set_pos(hsep, 0, SENSOR_PANEL_H);
	lv_obj_set_style_bg_color(hsep, lv_color_hex(THEME_HSEP_COLOR), LV_PART_MAIN);
	lv_obj_set_style_border_width(hsep, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(hsep, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(hsep, 0, LV_PART_MAIN);

	/* --- Nav bar (y=195, h=27) --- */
	lv_obj_t *nav_bg = lv_obj_create(screen);

	lv_obj_set_size(nav_bg, 320, 27);
	lv_obj_set_pos(nav_bg, 0, SENSOR_PANEL_H + 2);
	lv_obj_add_style(nav_bg, &g_style_nav_bg, LV_PART_MAIN);

	/* Prev button (left) */
	lv_obj_t *btn_prev = lv_button_create(nav_bg);

	lv_obj_set_size(btn_prev, 50, 23);
	lv_obj_set_pos(btn_prev, 2, 2);
	lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x2A3040), LV_PART_MAIN);
	lv_obj_set_style_border_width(btn_prev, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x444466), LV_PART_MAIN);
	lv_obj_set_style_radius(btn_prev, 3, LV_PART_MAIN);
	lv_obj_set_style_pad_all(btn_prev, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(btn_prev, nav_prev_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *lbl_prev = lv_label_create(btn_prev);

	lv_label_set_text(lbl_prev, LV_SYMBOL_LEFT);
	lv_obj_set_style_text_color(lbl_prev, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_align(lbl_prev, LV_ALIGN_CENTER, 0, 0);

	/* Page indicator (center-left) */
	nav_page_label = lv_label_create(nav_bg);
	lv_label_set_text(nav_page_label, "1/1");
	lv_obj_set_style_text_font(nav_page_label, &lv_font_montserrat_10, LV_PART_MAIN);
	lv_obj_set_style_text_color(nav_page_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
	lv_obj_set_size(nav_page_label, 60, 27);
	lv_obj_set_pos(nav_page_label, 56, 0);
	lv_obj_set_style_text_align(nav_page_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

	/* MENU label (center) */
	lv_obj_t *menu_label = lv_label_create(nav_bg);

	lv_label_set_text(menu_label, "MENU");
	lv_obj_set_style_text_font(menu_label, &lv_font_montserrat_10, LV_PART_MAIN);
	lv_obj_set_style_text_color(menu_label, lv_color_hex(0x888888), LV_PART_MAIN);
	lv_obj_set_size(menu_label, 100, 27);
	lv_obj_set_pos(menu_label, 110, 0);
	lv_obj_set_style_text_align(menu_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

	/* Next button (right) */
	lv_obj_t *btn_next = lv_button_create(nav_bg);

	lv_obj_set_size(btn_next, 50, 23);
	lv_obj_set_pos(btn_next, 268, 2);
	lv_obj_set_style_bg_color(btn_next, lv_color_hex(0x2A3040), LV_PART_MAIN);
	lv_obj_set_style_border_width(btn_next, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(btn_next, lv_color_hex(0x444466), LV_PART_MAIN);
	lv_obj_set_style_radius(btn_next, 3, LV_PART_MAIN);
	lv_obj_set_style_pad_all(btn_next, 0, LV_PART_MAIN);
	lv_obj_add_event_cb(btn_next, nav_next_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *lbl_next = lv_label_create(btn_next);

	lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
	lv_obj_set_style_text_color(lbl_next, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_align(lbl_next, LV_ALIGN_CENTER, 0, 0);

#if defined(CONFIG_LVGL_DISPLAY_SHOW_BUTTON_ROW)
	/* --- Physical button indicator row (y=222, h=18) --- */
	for (int i = 0; i < 4; i++) {
		lv_obj_t *btn_box = lv_obj_create(screen);

		lv_obj_set_size(btn_box, 76, 16);
		lv_obj_set_pos(btn_box, 2 + i * 80, 223);
		lv_obj_set_style_bg_color(btn_box, lv_color_hex(0x1E2330), LV_PART_MAIN);
		lv_obj_set_style_border_width(btn_box, 1, LV_PART_MAIN);
		lv_obj_set_style_border_color(btn_box, lv_color_hex(0x444466), LV_PART_MAIN);
		lv_obj_set_style_radius(btn_box, 3, LV_PART_MAIN);
		lv_obj_set_style_pad_all(btn_box, 0, LV_PART_MAIN);
	}
#endif /* CONFIG_LVGL_DISPLAY_SHOW_BUTTON_ROW */
}

/* --------------------------------------------------------------------------
 * Main-thread timer loop
 * -------------------------------------------------------------------------- */

void lvgl_display_run(void)
{
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	lvgl_lock();
	create_ui();
#if defined(CONFIG_LVGL_DISPLAY_SHOW_CLOCK)
	k_work_init_delayable(&clock_work, clock_tick);
	k_work_reschedule(&clock_work, K_NO_WAIT);
#endif
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
