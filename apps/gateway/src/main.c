/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (gateway)
 * @brief Gateway application entry point.
 *
 * Per ADR-008 RULE 4: main.c registers the log module and sleeps forever.
 * All application logic is initialised via SYS_INIT callbacks in library
 * modules, activated by the Kconfig options in prj.conf.
 *
 * Sensor event logging is provided by lib/sensor_event_log/
 * (CONFIG_SENSOR_EVENT_LOG=y in prj.conf).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if CONFIG_LVGL_DISPLAY
#	include <lvgl_display/lvgl_display.h>
#endif

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station gateway v0.1.0");
#if CONFIG_LVGL_DISPLAY
	lvgl_display_run(); /* never returns; SDL must run on main thread */
#else
	k_sleep(K_FOREVER);
#endif
	return 0;
}
