/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SENSOR_NODE_TX_SENSOR_NODE_TX_H_
#define SENSOR_NODE_TX_SENSOR_NODE_TX_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the sensor node TX library.
 * Called via SYS_INIT. Initializes lora_node, sets up zbus listeners,
 * and enables periodic TX.
 *
 * @return 0 on success, negative errno on failure.
 */
int sensor_node_tx_init(void);

/**
 * Enable periodic TX and start the collection timer.
 * PM-ready: future PM framework will call this on wake.
 *
 * @return 0 on success.
 */
int sensor_node_tx_enable(void);

/**
 * Disable periodic TX and cancel the collection timer.
 * PM-ready: future PM framework will call this before sleep.
 *
 * @return 0 on success.
 */
int sensor_node_tx_disable(void);

/**
 * Force an immediate transmission of all buffered readings.
 * Called by config_cmd handler on FORCE_TX command.
 *
 * @return 0 on success, -ENETDOWN if TX is disabled.
 */
int sensor_node_tx_force_tx(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_NODE_TX_SENSOR_NODE_TX_H_ */
