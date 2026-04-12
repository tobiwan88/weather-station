/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (integration test app)
 * @brief Integration test entry point — full gateway stack, no LVGL display.
 *
 * Mirrors apps/gateway but disables the LVGL display so the process does not
 * block on SDL and the pytest-twister-harness can interact via stdin/stdout.
 *
 * All subsystem logic (HTTP dashboard, MQTT, fake sensors, etc.) initialises
 * via SYS_INIT callbacks driven by prj.conf Kconfig selections.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(integration, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station integration test app");
	k_sleep(K_FOREVER);
	return 0;
}
