/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file theme.h
 * @brief Internal display theme: colors, symbols, styles, and value scaling.
 *
 * All visual design decisions (colors, fonts, sizes, ranges) live here so that
 * any future additional source files in lib/lvgl_display share the same look.
 * This header is NOT exported through include/lvgl_display/.
 *
 * theme_init() must be called once from create_ui(), inside lvgl_lock().
 */

#ifndef LVGL_DISPLAY_THEME_H_
#define LVGL_DISPLAY_THEME_H_

#include <lvgl.h>
#include <sensor_event/sensor_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Per-sensor-type accent color
 * -------------------------------------------------------------------------- */

/**
 * @brief Return the accent lv_color_t for a sensor type.
 *
 * Used for icon labels, value labels, and arc/bar indicator tracks.
 * Falls back to a neutral gray for unknown types.
 */
lv_color_t theme_sensor_color(enum sensor_type t);

/* --------------------------------------------------------------------------
 * Per-sensor-type icon symbol
 * -------------------------------------------------------------------------- */

/**
 * @brief Return an LV_SYMBOL_* string for a sensor type icon.
 *
 * The returned pointer is a string literal — never NULL.
 */
const char *theme_sensor_symbol(enum sensor_type t);

/* --------------------------------------------------------------------------
 * Value scaling for arc / bar widgets (0–1000 internal range)
 * -------------------------------------------------------------------------- */

/**
 * @brief Map a decoded physical value to the 0–1000 arc/bar range.
 *
 * Each type has a defined [min, max] physical range. Values outside
 * that range are clamped. The result is linear within [0, 1000].
 */
int theme_scale_value(enum sensor_type t, double v);

/* --------------------------------------------------------------------------
 * Reusable lv_style_t objects
 *
 * All styles are initialised by theme_init() and may be applied with
 *   lv_obj_add_style(obj, &g_style_card, LV_PART_MAIN)
 * -------------------------------------------------------------------------- */

/** Card container: dark background, rounded corners, subtle border. */
extern lv_style_t g_style_card;

/** Primary value label: montserrat_14, white. */
extern lv_style_t g_style_value_label;

/** Sensor name label: montserrat_10, muted gray. */
extern lv_style_t g_style_name_label;

/** Unit label: montserrat_10, darker gray. */
extern lv_style_t g_style_unit_label;

/** Nav bar background: dark gray, no border. */
extern lv_style_t g_style_nav_bg;

/* --------------------------------------------------------------------------
 * Clock style constants
 * -------------------------------------------------------------------------- */

#define THEME_CLOCK_BG_COLOR     0x111111u /**< Clock face fill             */
#define THEME_CLOCK_BORDER_COLOR 0x444466u /**< Clock face border (blue tint)*/
#define THEME_CLOCK_TICK_COLOR   0xCCCCCCu /**< Hour tick dot color         */
#define THEME_CLOCK_HOUR_WIDTH   5         /**< Hour hand line width (px)   */
#define THEME_CLOCK_MIN_WIDTH    3         /**< Minute hand line width (px) */
#define THEME_CLOCK_HOUR_COLOR   0xF0F0F0u /**< Hour hand color             */
#define THEME_CLOCK_MIN_COLOR    0xCCCCCCu /**< Minute hand color           */
#define THEME_CLOCK_CAP_SIZE     10        /**< Center cap diameter (px)    */
#define THEME_CLOCK_CAP_COLOR    0xFF6B35u /**< Center cap = temperature accent */

/* --------------------------------------------------------------------------
 * Screen / layout colors
 * -------------------------------------------------------------------------- */

#define THEME_SCREEN_BG_COLOR 0x0D1117u /**< Main screen background      */
#define THEME_DIVIDER_COLOR   0x333344u /**< Vertical panel divider      */
#define THEME_HSEP_COLOR      0x444455u /**< Horizontal separator bar    */

/* --------------------------------------------------------------------------
 * Initialise all g_style_* objects
 *
 * Must be called once from create_ui(), while holding lvgl_lock().
 * -------------------------------------------------------------------------- */
void theme_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_DISPLAY_THEME_H_ */
