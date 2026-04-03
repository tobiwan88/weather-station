/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (sensor-node)
 * @brief Sensor-node application entry point.
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

LOG_MODULE_REGISTER(sensor_node, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station sensor-node v0.1.0");
	k_sleep(K_FOREVER);
	return 0;
}
