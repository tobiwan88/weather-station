/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file pipe_transport.c
 * @brief Gateway-side FIFO transport. Creates the POSIX FIFO, opens it for
 *        reading, decodes length-prefixed protobuf SensorReading frames, and
 *        publishes measurements to sensor_event_chan via remote_sensor_publish_data().
 *
 *        Discovery is lazy: the first frame from a new UID triggers a
 *        REMOTE_DISCOVERY_FOUND announce so remote_sensor_manager can register
 *        it before data starts flowing.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>

#include "sensor_message.pb.h"
#include <pb_decode.h>
#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(pipe_transport, CONFIG_PIPE_TRANSPORT_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Per-UID seen table for lazy discovery
 * -------------------------------------------------------------------------- */

static uint32_t s_seen_uids[CONFIG_REMOTE_SENSOR_MAX_PEERS];

static bool uid_is_seen(uint32_t uid)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (s_seen_uids[i] == uid) {
			return true;
		}
	}
	return false;
}

static void uid_mark_seen(uint32_t uid)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (s_seen_uids[i] == 0) {
			s_seen_uids[i] = uid;
			return;
		}
	}
	LOG_WRN("seen_uids table full, uid=0x%08x not tracked", uid);
}

/* --------------------------------------------------------------------------
 * Transport vtable (pipe is receive-only; no scan, no trigger)
 * -------------------------------------------------------------------------- */

static int pipe_peer_add(const struct remote_transport *t, const uint8_t *peer_addr,
			 size_t addr_len, uint32_t uid)
{
	ARG_UNUSED(t);
	ARG_UNUSED(peer_addr);
	ARG_UNUSED(addr_len);
	ARG_UNUSED(uid);
	return 0;
}

static int pipe_peer_remove(const struct remote_transport *t, uint32_t uid)
{
	ARG_UNUSED(t);
	ARG_UNUSED(uid);
	return 0;
}

REMOTE_TRANSPORT_DEFINE(pipe_remote_transport, {
						       .name = "pipe",
						       .proto = REMOTE_TRANSPORT_PROTO_PIPE,
						       .caps = 0,
						       .scan_start = NULL,
						       .scan_stop = NULL,
						       .peer_add = pipe_peer_add,
						       .peer_remove = pipe_peer_remove,
						       .send_trigger = NULL,
					       });

/* --------------------------------------------------------------------------
 * Reader thread — non-blocking FIFO read loop with polling
 * -------------------------------------------------------------------------- */

/* Maximum time a partially received frame may stall before it is abandoned. */
#define FRAME_STALL_TIMEOUT_MS 2000

/* Read exactly @p len bytes from a non-blocking FIFO.
 *
 * With @p idle_ok, nothing received at all (no writer, or writer idle) returns
 * -EAGAIN so the caller can poll again. Once a frame has started, the read
 * waits for the rest of it; a writer that closes mid-frame returns -EPIPE and
 * a writer that stalls for FRAME_STALL_TIMEOUT_MS returns -ETIMEDOUT.
 */
static int fifo_read_exact(int fd, uint8_t *buf, size_t len, bool idle_ok)
{
	size_t got = 0;
	int64_t start = k_uptime_get();

	while (got < len) {
		ssize_t n = read(fd, buf + got, len - got);

		if (n > 0) {
			got += (size_t)n;
		} else if (n == 0) {
			return (idle_ok && got == 0) ? -EAGAIN : -EPIPE;
		} else if (errno == EAGAIN) {
			if (idle_ok && got == 0) {
				return -EAGAIN;
			}
			if (k_uptime_get() - start >= FRAME_STALL_TIMEOUT_MS) {
				return -ETIMEDOUT;
			}
			k_sleep(K_MSEC(100));
		} else {
			return -errno;
		}
	}

	return 0;
}

static void pipe_reader_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		/* O_NONBLOCK: open() returns immediately even when no writer is connected.
		 * The fd stays open, so a writer can connect at any time. */
		int fd = open(CONFIG_PIPE_TRANSPORT_FIFO_PATH, O_RDONLY | O_NONBLOCK);

		if (fd < 0) {
			LOG_WRN("fifo open failed (errno=%d), retrying", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("pipe_transport: fifo opened for reading");

		while (true) {
			/* Read 2-byte little-endian length prefix. */
			uint8_t len_buf[2];
			int rd = fifo_read_exact(fd, len_buf, sizeof(len_buf), true);

			if (rd == -EAGAIN) {
				/* No writer yet, or writer idle: poll again. */
				k_sleep(K_MSEC(100));
				continue;
			}
			if (rd == -EPIPE || rd == -ETIMEDOUT) {
				/* Writer left or stalled after part of a length prefix:
				 * the stream is out of sync, so drop it and reopen. */
				LOG_WRN("pipe_transport: partial length dropped (rc=%d), reopening",
					rd);
				break;
			}
			if (rd < 0) {
				LOG_WRN("fifo read error (rc=%d), reopening", rd);
				break;
			}

			uint16_t payload_len = (uint16_t)len_buf[0] | ((uint16_t)len_buf[1] << 8);

			if (payload_len == 0 || payload_len > SensorReading_size) {
				LOG_WRN("invalid payload_len=%u (max=%d), dropping frame and "
					"reopening",
					payload_len, SensorReading_size);
				break;
			}

			uint8_t payload[SensorReading_size];

			rd = fifo_read_exact(fd, payload, payload_len, false);
			if (rd < 0) {
				LOG_WRN("truncated payload (rc=%d want=%u), reopening", rd,
					payload_len);
				break;
			}

			SensorReading msg = SensorReading_init_zero;
			pb_istream_t istream = pb_istream_from_buffer(payload, payload_len);

			if (!pb_decode(&istream, SensorReading_fields, &msg)) {
				LOG_WRN("pb_decode: %s", PB_GET_ERROR(&istream));
				continue;
			}

			uint32_t uid = msg.sensor_uid;
			uint32_t type = msg.sensor_type;
			int32_t q31 = msg.q31_value;

			/* Announce new UIDs before publishing data. */
			if (!uid_is_seen(uid)) {
				struct remote_discovery_event disc = {
					.action = REMOTE_DISCOVERY_FOUND,
					.proto = REMOTE_TRANSPORT_PROTO_PIPE,
					.sensor_type = (enum sensor_type)type,
					.suggested_uid = uid,
					.peer_addr_len = 4,
				};

				memcpy(disc.peer_addr, &uid, sizeof(uid));
				snprintf(disc.suggested_label, sizeof(disc.suggested_label),
					 "pipe-%08x", uid);

				int rc = remote_sensor_announce_disc(&disc);

				if (rc == 0) {
					uid_mark_seen(uid);
					/* Allow remote_sensor_manager to process discovery
					 * before data arrives. */
					k_sleep(K_MSEC(100));
				} else {
					LOG_WRN("announce_disc uid=0x%08x failed: %d", uid, rc);
				}
			}

			int rc = remote_sensor_publish_data(uid, (enum sensor_type)type, q31);

			if (rc != 0) {
				LOG_WRN("publish_data uid=0x%08x failed: %d", uid, rc);
			} else {
				LOG_DBG("received uid=0x%08x type=%u q31=%d", uid, type, q31);
			}
		}

		close(fd);
		k_sleep(K_MSEC(100));
	}
}

K_THREAD_DEFINE(pipe_reader_tid, CONFIG_PIPE_TRANSPORT_THREAD_STACK_SIZE, pipe_reader_thread, NULL,
		NULL, NULL, CONFIG_PIPE_TRANSPORT_THREAD_PRIORITY, 0, 0);

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 93 — create FIFO before publisher connects
 * -------------------------------------------------------------------------- */

static int pipe_transport_init(void)
{
	int rc = mkfifo(CONFIG_PIPE_TRANSPORT_FIFO_PATH, 0600);

	if (rc != 0 && errno != EEXIST) {
		LOG_ERR("mkfifo '%s' failed (errno=%d)", CONFIG_PIPE_TRANSPORT_FIFO_PATH, errno);
		return -errno;
	}

	LOG_INF("pipe_transport: init done (fifo=%s)", CONFIG_PIPE_TRANSPORT_FIFO_PATH);
	return 0;
}

SYS_INIT(pipe_transport_init, APPLICATION, 93);
