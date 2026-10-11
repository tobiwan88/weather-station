/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME sensor_node_tx
#define LOG_LEVEL       CONFIG_SENSOR_NODE_TX_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>

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
	struct k_work force_work;
	struct env_sensor_data buffer[CONFIG_SENSOR_NODE_TX_MAX_READINGS];
	/* In-flight copy of the batch; only touched from the TX work context */
	struct env_sensor_data batch[CONFIG_SENSOR_NODE_TX_MAX_READINGS];
	struct lora_reading wire[CONFIG_SENSOR_NODE_TX_READINGS_PER_PACKET];
	uint16_t count;
	bool enabled;
	struct k_spinlock lock;
};

static struct tx_state state;

/* Forward declarations */
static void tx_work_fn(struct k_work *work);
static void force_work_fn(struct k_work *work);
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

	/* Buffer full: drop the oldest reading so the newest data is sent. */
	bool dropped = false;

	if (state.count >= CONFIG_SENSOR_NODE_TX_MAX_READINGS) {
		memmove(&state.buffer[0], &state.buffer[1],
			(state.count - 1) * sizeof(state.buffer[0]));
		state.count--;
		dropped = true;
	}

	state.buffer[state.count++] = *event;
	k_spin_unlock(&state.lock, key);

	if (dropped) {
		LOG_WRN_ONCE("TX buffer full, dropping oldest readings");
	}
}

ZBUS_LISTENER_DEFINE(sensor_node_tx_listener, sensor_event_handler);

ZBUS_CHAN_DECLARE(sensor_event_chan);

/* Config command handler — runs in publisher context, must not block.
 * The radio send is deferred to the work queue.
 */
static void config_cmd_handler(const struct zbus_channel *chan)
{
	const struct config_cmd_event *cmd = zbus_chan_const_msg(chan);

	if (cmd->cmd == CONFIG_CMD_FORCE_TX) {
		k_work_submit(&state.force_work);
	}
}

ZBUS_LISTENER_DEFINE(sensor_node_tx_cmd_listener, config_cmd_handler);

ZBUS_CHAN_DECLARE(config_cmd_chan);

/* Put unsent readings back at the front of the buffer. Oldest readings are
 * dropped first if the buffer is full.
 */
static void requeue_readings(const struct env_sensor_data *unsent, uint16_t num)
{
	k_spinlock_key_t key = k_spin_lock(&state.lock);

	uint16_t total = num + state.count;
	uint16_t skip = 0;

	if (total > CONFIG_SENSOR_NODE_TX_MAX_READINGS) {
		skip = total - CONFIG_SENSOR_NODE_TX_MAX_READINGS;
	}

	uint16_t keep = num - skip;

	memmove(&state.buffer[keep], &state.buffer[0], state.count * sizeof(state.buffer[0]));
	memcpy(&state.buffer[0], &unsent[skip], keep * sizeof(state.buffer[0]));
	state.count += keep;

	k_spin_unlock(&state.lock, key);

	if (skip > 0) {
		LOG_WRN("TX buffer full, dropped %d oldest readings", skip);
	}
}

/* Transmit all buffered readings in packets of at most READINGS_PER_PACKET.
 * Runs only in the work queue, so state.batch and state.wire are not shared.
 */
static void do_transmit(void)
{
	k_spinlock_key_t key = k_spin_lock(&state.lock);

	if (state.count == 0) {
		k_spin_unlock(&state.lock, key);
		LOG_DBG("no readings to transmit");
		return;
	}

	/* Copy out so the radio send happens outside the lock */
	uint16_t count = state.count;
	memcpy(state.batch, state.buffer, count * sizeof(state.buffer[0]));
	state.count = 0;
	k_spin_unlock(&state.lock, key);

	uint16_t sent = 0;
	int ret = 0;

	while (sent < count) {
		uint16_t chunk = MIN(CONFIG_SENSOR_NODE_TX_READINGS_PER_PACKET, count - sent);

		/* Convert to wire format */
		for (uint16_t i = 0; i < chunk; i++) {
			const struct env_sensor_data *src = &state.batch[sent + i];

			state.wire[i].type = (uint8_t)src->type;
			state.wire[i].q31_value = src->q31_value;
		}

		ret = lora_node_transmit((const uint8_t *)state.wire, chunk);
		if (ret < 0) {
			break;
		}
		sent += chunk;
	}

	if (sent == count) {
		LOG_INF("transmitted %d readings", count);
		return;
	}

	LOG_ERR("lora_node_transmit failed after %d of %d readings: %d", sent, count, ret);
	requeue_readings(&state.batch[sent], count - sent);
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

/* Forced TX work function (FORCE_TX config command and shell) */
static void force_work_fn(struct k_work *work)
{
	(void)work;

	do_transmit();
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
	k_work_submit(&state.force_work);
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

	state.count = 0;
	state.enabled = false;

	k_work_init_delayable(&state.timer, tx_work_fn);
	k_work_init(&state.force_work, force_work_fn);

	/* Listeners defined with ZBUS_LISTENER_DEFINE are not attached to any
	 * channel until added as observers.
	 */
	ret = zbus_chan_add_obs(&sensor_event_chan, &sensor_node_tx_listener, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("observe sensor_event_chan failed: %d", ret);
		return ret;
	}
	ret = zbus_chan_add_obs(&config_cmd_chan, &sensor_node_tx_cmd_listener, K_NO_WAIT);
	if (ret < 0) {
		LOG_ERR("observe config_cmd_chan failed: %d", ret);
		return ret;
	}

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
