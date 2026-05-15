/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(uart_lora_bridge, CONFIG_UART_LORA_BRIDGE_LOG_LEVEL);

/*
 * Wire frame format (24 bytes per env_sensor_data event):
 *   [0x5A][0xA5]  2-byte magic sync
 *   [len]         payload length byte (always WIRE_PAYLOAD_LEN = 20)
 *   [payload]     20-byte packed struct (LE, no padding)
 *   [crc8]        CRC8-CCITT over payload bytes
 *
 * The wire payload mirrors env_sensor_data with explicit LE layout so
 * it is independent of the host ABI struct padding.
 */

#define FRAME_MAGIC_0    0x5A
#define FRAME_MAGIC_1    0xA5
#define WIRE_PAYLOAD_LEN 20U /* 4+4+4+8, packed LE */

/* Packed wire representation — avoids ABI padding concerns. */
struct __packed uart_lora_wire {
	uint32_t sensor_uid;
	uint32_t type; /* enum sensor_type as uint32_t */
	int32_t q31_value;
	int64_t timestamp_ms;
};

BUILD_ASSERT(sizeof(struct uart_lora_wire) == WIRE_PAYLOAD_LEN, "uart_lora_wire size mismatch");

/* One byte at a time via ISR → msgq → thread state machine. */
static K_MSGQ_DEFINE(uart_rx_msgq, 1U, 256U, 1U);

static void uart_irq_cb(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
		return;
	}

	uint8_t byte;

	while (uart_fifo_read(dev, &byte, 1) == 1) {
		/* Drop silently if msgq full — framing will re-sync. */
		(void)k_msgq_put(&uart_rx_msgq, &byte, K_NO_WAIT);
	}
}

static void dispatch(const uint8_t *payload)
{
	struct uart_lora_wire wire;

	memcpy(&wire, payload, sizeof(wire));

	struct env_sensor_data evt = {
		.sensor_uid = wire.sensor_uid,
		.type = (enum sensor_type)wire.type,
		.q31_value = wire.q31_value,
		.timestamp_ms = wire.timestamp_ms,
	};

	int ret = zbus_chan_pub(&sensor_event_chan, &evt, K_MSEC(10));

	if (ret) {
		LOG_WRN("zbus publish failed: %d", ret);
	}
}

static void rx_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	enum { SYNC0, SYNC1, LEN, PAYLOAD, CRC_BYTE } state = SYNC0;
	uint8_t payload[WIRE_PAYLOAD_LEN];
	uint8_t payload_idx = 0;
	uint8_t byte;

	while (true) {
		k_msgq_get(&uart_rx_msgq, &byte, K_FOREVER);

		switch (state) {
		case SYNC0:
			if (byte == FRAME_MAGIC_0) {
				state = SYNC1;
			}
			break;

		case SYNC1:
			state = (byte == FRAME_MAGIC_1) ? LEN : SYNC0;
			break;

		case LEN:
			if (byte == WIRE_PAYLOAD_LEN) {
				payload_idx = 0;
				state = PAYLOAD;
			} else {
				LOG_WRN("bad frame len %u, resyncing", byte);
				state = SYNC0;
			}
			break;

		case PAYLOAD:
			payload[payload_idx++] = byte;
			if (payload_idx == WIRE_PAYLOAD_LEN) {
				state = CRC_BYTE;
			}
			break;

		case CRC_BYTE: {
			uint8_t expected = crc8_ccitt(0xFF, payload, WIRE_PAYLOAD_LEN);

			if (byte == expected) {
				dispatch(payload);
			} else {
				LOG_WRN("CRC mismatch: got 0x%02x expected 0x%02x", byte, expected);
			}
			state = SYNC0;
			break;
		}
		}
	}
}

K_THREAD_DEFINE(uart_lora_bridge_thread, CONFIG_UART_LORA_BRIDGE_RX_STACK_SIZE, rx_thread_fn, NULL,
		NULL, NULL, CONFIG_UART_LORA_BRIDGE_RX_PRIORITY, 0, 0);

static int uart_lora_bridge_init(void)
{
	const struct device *dev = device_get_binding(CONFIG_UART_LORA_BRIDGE_DEV_NAME);

	if (!dev) {
		LOG_ERR("UART device '%s' not found", CONFIG_UART_LORA_BRIDGE_DEV_NAME);
		return -ENODEV;
	}

	uart_irq_callback_set(dev, uart_irq_cb);
	uart_irq_rx_enable(dev);

	LOG_INF("UART LoRa bridge ready on %s", CONFIG_UART_LORA_BRIDGE_DEV_NAME);
	return 0;
}

SYS_INIT(uart_lora_bridge_init, APPLICATION, 90);
