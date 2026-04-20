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
	/*
	 * All SYS_INIT callbacks (HTTP server, MQTT publisher, fake sensors, …)
	 * have completed by the time main() runs.  Emit a well-known sentinel
	 * that the pytest conftest waits for before starting any test:
	 *
	 *   [00:00:00.xxx] <inf> integration: device: ready
	 *
	 * grep pattern used by the device_ready fixture:
	 *   r"integration:.*device:.*ready"
	 */
	k_sleep(K_SECONDS(1));
	LOG_INF("device: ready");
	return 0;
}
