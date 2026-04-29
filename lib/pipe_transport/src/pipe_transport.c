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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>

#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(pipe_transport, CONFIG_PIPE_TRANSPORT_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Manual protobuf varint decoder
 * -------------------------------------------------------------------------- */

static int varint_decode(const uint8_t *buf, size_t len, uint64_t *out)
{
	*out = 0;
	for (int i = 0; i < (int)len && i < 10; i++) {
		*out |= (uint64_t)(buf[i] & 0x7F) << (7 * i);
		if (!(buf[i] & 0x80)) {
			return i + 1;
		}
	}
	return -EINVAL;
}

static int sensor_reading_decode(const uint8_t *buf, size_t len, uint32_t *uid, uint32_t *type,
				 int32_t *q31, int64_t *ts_ms)
{
	*uid = 0;
	*type = 0;
	*q31 = 0;
	*ts_ms = 0;

	size_t pos = 0;

	while (pos < len) {
		uint64_t tag_val;
		int consumed = varint_decode(buf + pos, len - pos, &tag_val);

		if (consumed < 0) {
			return -EINVAL;
		}
		pos += consumed;

		uint32_t field_num = (uint32_t)(tag_val >> 3);
		uint64_t value;

		consumed = varint_decode(buf + pos, len - pos, &value);
		if (consumed < 0) {
			return -EINVAL;
		}
		pos += consumed;

		switch (field_num) {
		case 1:
			*uid = (uint32_t)value;
			break;
		case 2:
			*type = (uint32_t)value;
			break;
		case 3:
			*q31 = (int32_t)(uint32_t)value;
			break;
		case 4:
			*ts_ms = (int64_t)value;
			break;
		default:
			/* unknown field, skip */
			break;
		}
	}
	return 0;
}

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
 * Transport vtable (no-op stubs — pipe is receive-only)
 * -------------------------------------------------------------------------- */

static int pipe_scan_start(const struct remote_transport *t)
{
	ARG_UNUSED(t);
	return 0;
}

static int pipe_scan_stop(const struct remote_transport *t)
{
	ARG_UNUSED(t);
	return 0;
}

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
						       .caps = REMOTE_TRANSPORT_CAP_SCAN,
						       .scan_start = pipe_scan_start,
						       .scan_stop = pipe_scan_stop,
						       .peer_add = pipe_peer_add,
						       .peer_remove = pipe_peer_remove,
						       .send_trigger = NULL,
					       });

/* --------------------------------------------------------------------------
 * Reader thread — blocking FIFO read loop
 * -------------------------------------------------------------------------- */

#define PAYLOAD_MAX 64

static void pipe_reader_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		/* open() blocks until the write end is opened (correct FIFO semantics). */
		int fd = open(CONFIG_PIPE_TRANSPORT_FIFO_PATH, O_RDONLY);

		if (fd < 0) {
			LOG_WRN("fifo open failed (errno=%d), retrying", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("pipe_transport: fifo opened for reading");

		while (true) {
			/* Read 2-byte little-endian length prefix. */
			uint8_t len_buf[2];
			ssize_t n = read(fd, len_buf, sizeof(len_buf));

			if (n == 0) {
				/* EOF — writer closed its end. */
				LOG_INF("pipe_transport: writer disconnected, reopening");
				break;
			}
			if (n < 0) {
				LOG_WRN("fifo read error (errno=%d)", errno);
				break;
			}
			if (n < 2) {
				/* Partial length prefix — discard and reopen. */
				LOG_WRN("partial length prefix (%zd bytes), discarding", n);
				break;
			}

			uint16_t payload_len = (uint16_t)len_buf[0] | ((uint16_t)len_buf[1] << 8);

			if (payload_len > PAYLOAD_MAX) {
				LOG_WRN("payload_len=%u exceeds max=%d, discarding frame",
					payload_len, PAYLOAD_MAX);
				break;
			}

			uint8_t payload[PAYLOAD_MAX];
			size_t received = 0;

			while (received < payload_len) {
				n = read(fd, payload + received, payload_len - received);
				if (n <= 0) {
					break;
				}
				received += (size_t)n;
			}

			if (received < payload_len) {
				LOG_WRN("truncated payload (got=%zu want=%u), discarding", received,
					payload_len);
				break;
			}

			uint32_t uid, type;
			int32_t q31;
			int64_t ts_ms;

			if (sensor_reading_decode(payload, payload_len, &uid, &type, &q31,
						  &ts_ms) != 0) {
				LOG_WRN("protobuf decode failed, discarding frame");
				continue;
			}

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
