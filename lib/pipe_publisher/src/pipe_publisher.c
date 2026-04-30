/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file pipe_publisher.c
 * @brief Sensor-node side FIFO publisher. Subscribes to sensor_event_chan
 *        and writes length-prefixed protobuf SensorReading frames to a POSIX
 *        FIFO for native_sim integration testing.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(pipe_publisher, CONFIG_PIPE_PUBLISHER_LOG_LEVEL);

ZBUS_CHAN_DECLARE(sensor_event_chan);
ZBUS_SUBSCRIBER_DEFINE(pipe_pub_sub, 8);

/* 64-byte payload + 2-byte LE length prefix */
#define PAYLOAD_MAX 64
#define TX_BUF_SIZE (PAYLOAD_MAX + 2)

static int s_fifo_fd = -1;
static uint8_t s_tx_buf[TX_BUF_SIZE];

/* --------------------------------------------------------------------------
 * Manual protobuf varint encoder
 * -------------------------------------------------------------------------- */

static int varint_encode(uint8_t *buf, uint64_t v)
{
	int n = 0;

	while (v > 0x7FU) {
		buf[n++] = (uint8_t)((v & 0x7FU) | 0x80U);
		v >>= 7;
	}
	buf[n++] = (uint8_t)v;
	return n;
}

static int sensor_reading_encode(uint8_t *buf, size_t buf_size, uint32_t uid, uint32_t type,
				 int32_t q31, int64_t ts_ms)
{
	uint8_t tmp[64];
	int n = 0;

#define TAG(f) ((uint64_t)((unsigned)(f) << 3))
	n += varint_encode(tmp + n, TAG(1));
	n += varint_encode(tmp + n, uid);
	n += varint_encode(tmp + n, TAG(2));
	n += varint_encode(tmp + n, type);
	n += varint_encode(tmp + n, TAG(3));
	n += varint_encode(tmp + n, (uint64_t)(int64_t)q31);
	n += varint_encode(tmp + n, TAG(4));
	n += varint_encode(tmp + n, (uint64_t)ts_ms);
#undef TAG

	if ((size_t)n > buf_size) {
		return -ENOMEM;
	}
	memcpy(buf, tmp, n);
	return n;
}

/* --------------------------------------------------------------------------
 * FIFO open with retry
 * -------------------------------------------------------------------------- */

static void ensure_open(void)
{
	if (s_fifo_fd >= 0) {
		return;
	}

	/* O_NONBLOCK so open() does not block waiting for the read end. */
	s_fifo_fd = open(CONFIG_PIPE_PUBLISHER_FIFO_PATH, O_WRONLY | O_NONBLOCK);
	if (s_fifo_fd < 0) {
		/* Read end not yet open — caller retries later. */
		LOG_DBG("fifo open pending (errno=%d)", errno);
	}
}

/* --------------------------------------------------------------------------
 * Subscriber thread
 * -------------------------------------------------------------------------- */

static void pipe_pub_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	const struct zbus_channel *chan;

	while (true) {
		int rc = zbus_sub_wait(&pipe_pub_sub, &chan, K_FOREVER);

		if (rc != 0) {
			LOG_ERR("zbus_sub_wait: %d", rc);
			continue;
		}

		struct env_sensor_data evt;

		if (zbus_chan_read(&sensor_event_chan, &evt, K_NO_WAIT) != 0) {
			continue;
		}

		/* Retry open if FIFO not yet connected to a reader. */
		if (s_fifo_fd < 0) {
			ensure_open();
			if (s_fifo_fd < 0) {
				k_sleep(K_MSEC(100));
				ensure_open();
				if (s_fifo_fd < 0) {
					LOG_WRN("fifo not open, dropping event uid=0x%08x",
						evt.sensor_uid);
					continue;
				}
			}
		}

		uint8_t payload[PAYLOAD_MAX];
		int plen =
			sensor_reading_encode(payload, sizeof(payload), evt.sensor_uid,
					      (uint32_t)evt.type, evt.q31_value, evt.timestamp_ms);

		if (plen < 0) {
			LOG_ERR("encode failed: %d", plen);
			continue;
		}

		/* 2-byte little-endian length prefix. */
		s_tx_buf[0] = (uint8_t)(plen & 0xFF);
		s_tx_buf[1] = (uint8_t)((plen >> 8) & 0xFF);
		memcpy(s_tx_buf + 2, payload, plen);

		ssize_t written = write(s_fifo_fd, s_tx_buf, (size_t)(plen + 2));

		if (written < 0) {
			LOG_WRN("fifo write failed (errno=%d), closing", errno);
			close(s_fifo_fd);
			s_fifo_fd = -1;
		} else {
			LOG_DBG("published uid=0x%08x type=%u q31=%d (%d bytes)", evt.sensor_uid,
				(unsigned)evt.type, evt.q31_value, plen);
		}
	}
}

K_THREAD_DEFINE(pipe_pub_tid, CONFIG_PIPE_PUBLISHER_THREAD_STACK_SIZE, pipe_pub_thread, NULL, NULL,
		NULL, CONFIG_PIPE_PUBLISHER_THREAD_PRIORITY, 0, 0);

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 94 — subscribe to sensor_event_chan
 * -------------------------------------------------------------------------- */

static int pipe_publisher_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &pipe_pub_sub, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("add obs sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("pipe_publisher: init done (fifo=%s)", CONFIG_PIPE_PUBLISHER_FIFO_PATH);
	return 0;
}

SYS_INIT(pipe_publisher_init, APPLICATION, 94);
