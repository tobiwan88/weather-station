/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME sensor_node_tx
#define LOG_LEVEL       CONFIG_SENSOR_NODE_TX_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <config_cmd/config_cmd.h>
#include <lora_node/lora_node.h>
#include <sensor_event/sensor_event.h>

/* Wire format: 1B sensor_type + 4B q31_value (5 bytes, matches lora_frame.h) */
struct lora_reading {
	uint8_t type;
	int32_t q31_value;
} __packed;

BUILD_ASSERT(sizeof(struct lora_reading) == 5, "reading must be 5 bytes");

/* TX state */
struct tx_state {
	struct k_work_delayable timer;
	struct env_sensor_data buffer[CONFIG_SENSOR_NODE_TX_MAX_READINGS];
	struct lora_reading wire[CONFIG_SENSOR_NODE_TX_MAX_READINGS];
	uint16_t count;
	bool enabled;
	struct k_spinlock lock;
};

static struct tx_state state;

/* Forward declarations */
static void tx_work_fn(struct k_work *work);
static void do_transmit(void);

/* zbus listener callback — runs in publisher context, must not block */
static void sensor_event_handler(const struct zbus_channel *chan)
{
	const struct env_sensor_data *event = zbus_chan_const_msg(chan);
	k_spinlock_key_t key = k_spin_lock(&state.lock);

	if (!state.enabled) {
		k_spin_unlock(&state.lock, key);
		return;
	}

	if (state.count >= CONFIG_SENSOR_NODE_TX_MAX_READINGS) {
		LOG_WRN("TX buffer full (%d), dropping event", state.count);
		k_spin_unlock(&state.lock, key);
		return;
	}

	state.buffer[state.count++] = *event;
	k_spin_unlock(&state.lock, key);
}

ZBUS_LISTENER_DEFINE(sensor_node_tx_listener, sensor_event_handler);

ZBUS_CHAN_DECLARE(sensor_event_chan);

/* Config command handler */
static void config_cmd_handler(const struct zbus_channel *chan)
{
	const struct config_cmd_event *cmd = zbus_chan_const_msg(chan);

	if (cmd->cmd == CONFIG_CMD_FORCE_TX) {
		do_transmit();
	}
}

ZBUS_LISTENER_DEFINE(sensor_node_tx_cmd_listener, config_cmd_handler);

ZBUS_CHAN_DECLARE(config_cmd_chan);

/* Transmit all buffered readings */
static void do_transmit(void)
{
	k_spinlock_key_t key = k_spin_lock(&state.lock);

	if (state.count == 0) {
		k_spin_unlock(&state.lock, key);
		LOG_DBG("no readings to transmit");
		return;
	}

	/* Convert to wire format */
	for (uint16_t i = 0; i < state.count; i++) {
		state.wire[i].type = (uint8_t)state.buffer[i].type;
		state.wire[i].q31_value = state.buffer[i].q31_value;
	}

	uint16_t count = state.count;
	state.count = 0;
	k_spin_unlock(&state.lock, key);

	int ret = lora_node_transmit((const uint8_t *)state.wire, count);
	if (ret < 0) {
		LOG_ERR("lora_node_transmit failed: %d", ret);
	} else {
		LOG_INF("transmitted %d readings", count);
	}
}

/* Timer work function */
static void tx_work_fn(struct k_work *work)
{
	(void)work;

	if (!state.enabled) {
		goto reschedule;
	}

	do_transmit();

reschedule:
	k_work_reschedule(&state.timer, K_SECONDS(CONFIG_SENSOR_NODE_TX_INTERVAL_S));
}

/* Public API */

int sensor_node_tx_enable(void)
{
	state.enabled = true;
	k_work_reschedule(&state.timer, K_SECONDS(CONFIG_SENSOR_NODE_TX_INTERVAL_S));
	LOG_INF("sensor_node_tx enabled (interval=%ds)", CONFIG_SENSOR_NODE_TX_INTERVAL_S);
	return 0;
}

int sensor_node_tx_disable(void)
{
	state.enabled = false;
	k_work_cancel_delayable(&state.timer);
	LOG_INF("sensor_node_tx disabled");
	return 0;
}

int sensor_node_tx_force_tx(void)
{
	if (!state.enabled) {
		return -ENETDOWN;
	}
	do_transmit();
	return 0;
}

/* SYS_INIT */

static int sensor_node_tx_init(void)
{
	int ret = lora_node_init();
	if (ret < 0) {
		LOG_ERR("lora_node_init failed: %d", ret);
		return ret;
	}

	k_spinlock_init(&state.lock);
	state.count = 0;
	state.enabled = false;

	k_work_init_delayable(&state.timer, tx_work_fn);

	/* Enable and start timer */
	sensor_node_tx_enable();

	return 0;
}

SYS_INIT(sensor_node_tx_init, APPLICATION, 90);

/* --------------------------------------------------------------------------
 * Shell commands
 * -------------------------------------------------------------------------- */

static int cmd_force(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	int ret = sensor_node_tx_force_tx();
	if (ret < 0) {
		shell_error(sh, "force_tx failed: %d", ret);
		return ret;
	}
	shell_print(sh, "Force TX triggered");
	return 0;
}

static int cmd_enable(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	sensor_node_tx_enable();
	shell_print(sh, "sensor_node_tx enabled");
	return 0;
}

static int cmd_disable(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	sensor_node_tx_disable();
	shell_print(sh, "sensor_node_tx disabled");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;

	k_spinlock_key_t key = k_spin_lock(&state.lock);
	uint16_t count = state.count;
	bool enabled = state.enabled;
	k_spin_unlock(&state.lock, key);

	shell_print(sh, "sensor_node_tx: %s (buffer: %d/%d)", enabled ? "enabled" : "disabled",
		    count, CONFIG_SENSOR_NODE_TX_MAX_READINGS);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sensor_node_tx_cmds,
	SHELL_CMD_ARG(force, NULL, "Force immediate transmission of buffered readings", cmd_force,
		      1, 0),
	SHELL_CMD_ARG(enable, NULL, "Enable periodic TX", cmd_enable, 1, 0),
	SHELL_CMD_ARG(disable, NULL, "Disable periodic TX", cmd_disable, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show TX subsystem status", cmd_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sensor_node_tx, &sensor_node_tx_cmds, "Sensor node LoRa TX controls", NULL);
