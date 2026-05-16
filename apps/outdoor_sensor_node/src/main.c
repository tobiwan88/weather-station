/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (outdoor_sensor_node)
 *
 * Outdoor sensor node for the Wio-E5 Mini (STM32WLE5JC).
 *
 * Reads BME688 sensor via I2C, encodes readings in compact 5-byte wire
 * format (1B sensor_type + 4B q31_value), and transmits via LoRa P2P
 * to the gateway. Sleeps between measurement cycles.
 *
 * Per ADR-008 RULE 4: main.c registers the log module and sleeps forever.
 * All application logic is initialized via SYS_INIT callbacks.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(outdoor_sensor_node, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("weather-station outdoor_sensor_node v0.1.0");
	k_sleep(K_FOREVER);
	return 0;
}
