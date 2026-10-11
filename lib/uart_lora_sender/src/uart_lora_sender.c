/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(uart_lora_sender, CONFIG_UART_LORA_SENDER_LOG_LEVEL);

#define FRAME_MAGIC_0    0x5A
#define FRAME_MAGIC_1    0xA5
#define WIRE_PAYLOAD_LEN 20U

/* Packed wire representation — mirrors env_sensor_data with explicit LE layout. */
struct __packed uart_lora_wire {
	uint32_t sensor_uid;
	uint32_t type;
	int32_t q31_value;
	int64_t timestamp_ms;
};

BUILD_ASSERT(sizeof(struct uart_lora_wire) == WIRE_PAYLOAD_LEN, "uart_lora_wire size mismatch");

ZBUS_CHAN_DECLARE(sensor_event_chan);
K_MSGQ_DEFINE_STATIC(uart_lora_sender_q, sizeof(struct env_sensor_data), 8, 4);

/* Cumulative count of events dropped because the queue was full. */
static atomic_t queue_drops = ATOMIC_INIT(0);

static const struct device *uart_dev;

static bool is_power_of_ten(uint32_t n)
{
	if (n == 0U) {
		return false;
	}
	while (n % 10U == 0U) {
		n /= 10U;
	}
	return n == 1U;
}

/* Runs in publisher context, must not block. Copy the event into the queue
 * that the sender thread drains.
 */
static void sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	if (k_msgq_put(&uart_lora_sender_q, evt, K_NO_WAIT) != 0) {
		/* Log at 1, 10, 100, ... drops to avoid spamming. */
		uint32_t n = (uint32_t)atomic_inc(&queue_drops) + 1U;

		if (is_power_of_ten(n)) {
			LOG_WRN("sender queue full, dropped %u events", n);
		}
	}
}

ZBUS_LISTENER_DEFINE(uart_lora_sender_listener, sensor_event_cb);

static void send_frame(const struct env_sensor_data *evt)
{
	struct uart_lora_wire wire = {
		.sensor_uid = evt->sensor_uid,
		.type = (uint32_t)evt->type,
		.q31_value = evt->q31_value,
		.timestamp_ms = evt->timestamp_ms,
	};

	uint8_t crc = crc8_ccitt(0xFF, (uint8_t *)&wire, WIRE_PAYLOAD_LEN);

	uart_poll_out(uart_dev, FRAME_MAGIC_0);
	uart_poll_out(uart_dev, FRAME_MAGIC_1);
	uart_poll_out(uart_dev, WIRE_PAYLOAD_LEN);
	for (size_t i = 0; i < WIRE_PAYLOAD_LEN; i++) {
		uart_poll_out(uart_dev, ((uint8_t *)&wire)[i]);
	}
	uart_poll_out(uart_dev, crc);
}

static void uart_lora_sender_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct env_sensor_data evt;
	int ret;

	while (true) {
		ret = k_msgq_get(&uart_lora_sender_q, &evt, K_FOREVER);
		if (ret) {
			LOG_ERR("k_msgq_get error: %d", ret);
			continue;
		}

		send_frame(&evt);
	}
}

K_THREAD_DEFINE(uart_lora_sender_thread_id, CONFIG_UART_LORA_SENDER_THREAD_STACK_SIZE,
		uart_lora_sender_thread, NULL, NULL, NULL, CONFIG_UART_LORA_SENDER_THREAD_PRIORITY,
		0, 0);

static int uart_lora_sender_init(void)
{
	uart_dev = device_get_binding(CONFIG_UART_LORA_SENDER_DEV_NAME);
	if (!uart_dev) {
		LOG_ERR("UART device '%s' not found", CONFIG_UART_LORA_SENDER_DEV_NAME);
		return -ENODEV;
	}

	int rc = zbus_chan_add_obs(&sensor_event_chan, &uart_lora_sender_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("add obs sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("UART LoRa sender ready on %s", CONFIG_UART_LORA_SENDER_DEV_NAME);
	return 0;
}

SYS_INIT(uart_lora_sender_init, APPLICATION, 92);
