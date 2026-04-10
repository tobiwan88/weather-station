/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file theme.c
 * @brief Display theme implementation: accent colors, icons, value scaling,
 *        and reusable LVGL style objects.
 *
 * Adding a new sensor type only requires extending the three lookup tables
 * (TYPE_COLORS, TYPE_SYMBOLS, TYPE_RANGES) and the SENSOR_TYPE_COUNT guard.
 */

#include "theme.h"

#include <lvgl.h>
#include <sensor_event/sensor_event.h>

/* -------------------------------------------------------------------------
 * Per-type accent colors (indexed by enum sensor_type)
 * ------------------------------------------------------------------------- */

static const uint32_t TYPE_COLORS[] = {
	[SENSOR_TYPE_TEMPERATURE] = 0xFF6B35, /* warm orange   */
	[SENSOR_TYPE_HUMIDITY] = 0x4FC3F7,    /* sky blue      */
	[SENSOR_TYPE_PRESSURE] = 0xAB47BC,    /* muted purple  */
	[SENSOR_TYPE_CO2] = 0xEF5350,         /* alert red     */
	[SENSOR_TYPE_VOC] = 0xFFA726,         /* amber         */
	[SENSOR_TYPE_LIGHT] = 0xFFEE58,       /* yellow        */
	[SENSOR_TYPE_UV_INDEX] = 0x66BB6A,    /* green         */
	[SENSOR_TYPE_BATTERY_MV] = 0x26C6DA,  /* teal          */
};

lv_color_t theme_sensor_color(enum sensor_type t)
{
	if ((unsigned)t < ARRAY_SIZE(TYPE_COLORS)) {
		return lv_color_hex(TYPE_COLORS[t]);
	}
	return lv_color_hex(0x888888); /* fallback: neutral gray */
}

/* -------------------------------------------------------------------------
 * Per-type icon symbols (indexed by enum sensor_type)
 * ------------------------------------------------------------------------- */

static const char *const TYPE_SYMBOLS[] = {
	[SENSOR_TYPE_TEMPERATURE] = LV_SYMBOL_WARNING,     /* closest to thermometer */
	[SENSOR_TYPE_HUMIDITY] = LV_SYMBOL_WIFI,           /* wave-like              */
	[SENSOR_TYPE_PRESSURE] = LV_SYMBOL_SETTINGS,       /* general indicator      */
	[SENSOR_TYPE_CO2] = LV_SYMBOL_WARNING,             /* alert: elevated CO2    */
	[SENSOR_TYPE_VOC] = LV_SYMBOL_TINT,                /* air quality            */
	[SENSOR_TYPE_LIGHT] = LV_SYMBOL_IMAGE,             /* closest to light       */
	[SENSOR_TYPE_UV_INDEX] = LV_SYMBOL_DOWNLOAD,       /* sun-like shape         */
	[SENSOR_TYPE_BATTERY_MV] = LV_SYMBOL_BATTERY_FULL, /* battery                */
};

const char *theme_sensor_symbol(enum sensor_type t)
{
	if ((unsigned)t < ARRAY_SIZE(TYPE_SYMBOLS)) {
		return TYPE_SYMBOLS[t];
	}
	return LV_SYMBOL_SETTINGS;
}

/* -------------------------------------------------------------------------
 * Per-type physical ranges for arc/bar scaling
 * map physical value → 0..1000 (clamped, linear)
 * ------------------------------------------------------------------------- */

struct type_range {
	double min;
	double max;
};

static const struct type_range TYPE_RANGES[] = {
	[SENSOR_TYPE_TEMPERATURE] = {-20.0, 60.0},   /* °C              */
	[SENSOR_TYPE_HUMIDITY] = {0.0, 100.0},       /* %RH             */
	[SENSOR_TYPE_PRESSURE] = {950.0, 1050.0},    /* hPa             */
	[SENSOR_TYPE_CO2] = {400.0, 2000.0},         /* ppm             */
	[SENSOR_TYPE_VOC] = {0.0, 500.0},            /* IAQ index       */
	[SENSOR_TYPE_LIGHT] = {0.0, 10000.0},        /* lux             */
	[SENSOR_TYPE_UV_INDEX] = {0.0, 11.0},        /* dimensionless   */
	[SENSOR_TYPE_BATTERY_MV] = {3000.0, 4200.0}, /* mV              */
};

int theme_scale_value(enum sensor_type t, double v)
{
	if ((unsigned)t >= ARRAY_SIZE(TYPE_RANGES)) {
		return 0;
	}
	double mn = TYPE_RANGES[t].min;
	double mx = TYPE_RANGES[t].max;

	if (v <= mn) {
		return 0;
	}
	if (v >= mx) {
		return 1000;
	}
	return (int)((v - mn) / (mx - mn) * 1000.0);
}

/* -------------------------------------------------------------------------
 * Reusable lv_style_t objects
 * ------------------------------------------------------------------------- */

lv_style_t g_style_card;
lv_style_t g_style_value_label;
lv_style_t g_style_name_label;
lv_style_t g_style_unit_label;
lv_style_t g_style_nav_bg;

void theme_init(void)
{
	/* Card container */
	lv_style_init(&g_style_card);
	lv_style_set_bg_color(&g_style_card, lv_color_hex(0x1A1F2E));
	lv_style_set_bg_opa(&g_style_card, LV_OPA_COVER);
	lv_style_set_radius(&g_style_card, 4);
	lv_style_set_border_width(&g_style_card, 1);
	lv_style_set_border_color(&g_style_card, lv_color_hex(0x2A3040));
	lv_style_set_pad_all(&g_style_card, 3);
	lv_style_set_pad_column(&g_style_card, 3);

	/* Primary value label */
	lv_style_init(&g_style_value_label);
	lv_style_set_text_font(&g_style_value_label, &lv_font_montserrat_14);
	lv_style_set_text_color(&g_style_value_label, lv_color_hex(0xFFFFFF));

	/* Sensor name label (small, muted) */
	lv_style_init(&g_style_name_label);
	lv_style_set_text_font(&g_style_name_label, &lv_font_montserrat_10);
	lv_style_set_text_color(&g_style_name_label, lv_color_hex(0x888888));

	/* Unit label (small, darker) */
	lv_style_init(&g_style_unit_label);
	lv_style_set_text_font(&g_style_unit_label, &lv_font_montserrat_10);
	lv_style_set_text_color(&g_style_unit_label, lv_color_hex(0x666666));

	/* Nav bar background */
	lv_style_init(&g_style_nav_bg);
	lv_style_set_bg_color(&g_style_nav_bg, lv_color_hex(0x1E2330));
	lv_style_set_bg_opa(&g_style_nav_bg, LV_OPA_COVER);
	lv_style_set_border_width(&g_style_nav_bg, 0);
	lv_style_set_radius(&g_style_nav_bg, 0);
	lv_style_set_pad_all(&g_style_nav_bg, 0);
}
