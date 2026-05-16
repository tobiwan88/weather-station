/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
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
ZBUS_SUBSCRIBER_DEFINE(uart_lora_sender_sub, 4);

static const struct device *uart_dev;

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

	const struct zbus_channel *chan;
	struct env_sensor_data evt;
	int ret;

	while (true) {
		ret = zbus_sub_wait(&uart_lora_sender_sub, &chan, K_FOREVER);
		if (ret) {
			LOG_ERR("zbus_sub_wait error: %d", ret);
			continue;
		}

		ret = zbus_chan_read(&sensor_event_chan, &evt, K_NO_WAIT);
		if (ret) {
			LOG_WRN("zbus_chan_read failed: %d", ret);
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

	int rc = zbus_chan_add_obs(&sensor_event_chan, &uart_lora_sender_sub, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("add obs sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("UART LoRa sender ready on %s", CONFIG_UART_LORA_SENDER_DEV_NAME);
	return 0;
}

SYS_INIT(uart_lora_sender_init, APPLICATION, 92);
