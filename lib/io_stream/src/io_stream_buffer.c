/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file io_stream_buffer.c
 * @brief RAM buffer stream backend.
 *
 * Wraps a caller-provided buffer with full read/write/seek/tell support.
 * Write beyond capacity returns -ENOSPC. Read past valid data returns 0 (EOF).
 *
 * Constrained by ADR-017 (Generic I/O Stream Abstraction).
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <io_stream/io_stream.h>

LOG_MODULE_REGISTER(io_stream_buffer, CONFIG_IO_STREAM_LOG_LEVEL);

static ssize_t buffer_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;

	if (d->pos >= (int32_t)d->data_len) {
		return 0; /* EOF */
	}

	size_t available = d->data_len - (size_t)d->pos;
	size_t to_read = MIN(len, available);

	memcpy(buf, d->buf + d->pos, to_read);
	d->pos += (int32_t)to_read;

	LOG_DBG("buffer read: %zu bytes (pos=%d, data_len=%zu)", to_read, d->pos, d->data_len);
	return (ssize_t)to_read;
}

static ssize_t buffer_write(struct io_stream *s, const uint8_t *data, size_t len)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;

	if (d->pos < 0) {
		return -EINVAL;
	}

	if ((size_t)d->pos + len > d->capacity) {
		size_t available = d->capacity - (size_t)d->pos;
		if (available == 0) {
			return -ENOSPC;
		}
		len = available;
	}

	memcpy(d->buf + d->pos, data, len);
	d->pos += (int32_t)len;

	if ((size_t)d->pos > d->data_len) {
		d->data_len = (size_t)d->pos;
	}

	LOG_DBG("buffer write: %zu bytes (pos=%d, data_len=%zu)", len, d->pos, d->data_len);
	return (ssize_t)len;
}

static int buffer_seek(struct io_stream *s, int32_t offset, int whence)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;
	int32_t new_pos;

	switch (whence) {
	case IO_STREAM_SEEK_SET:
		new_pos = offset;
		break;
	case IO_STREAM_SEEK_CUR:
		new_pos = d->pos + offset;
		break;
	case IO_STREAM_SEEK_END:
		new_pos = (int32_t)d->data_len + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0 || (size_t)new_pos > d->capacity) {
		return -EINVAL;
	}

	d->pos = new_pos;
	LOG_DBG("buffer seek: pos=%d (whence=%d, offset=%d)", d->pos, whence, offset);
	return 0;
}

static int32_t buffer_tell(struct io_stream *s)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;
	return d->pos;
}

static size_t buffer_size(struct io_stream *s)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;
	return d->data_len;
}

static int buffer_flush(struct io_stream *s)
{
	ARG_UNUSED(s);
	return 0;
}

static int buffer_close(struct io_stream *s)
{
	struct io_stream_buffer_state *d = (struct io_stream_buffer_state *)s->user_data;
	d->pos = 0;
	d->data_len = 0;
	LOG_DBG("buffer closed");
	return 0;
}

int io_stream_buffer_init(struct io_stream *s, struct io_stream_buffer_state *state, void *buf,
			  size_t capacity)
{
	if (!s || !state || !buf || capacity == 0) {
		return -EINVAL;
	}

	state->buf = (uint8_t *)buf;
	state->capacity = capacity;
	state->pos = 0;
	state->data_len = 0;

	s->read = buffer_read;
	s->write = buffer_write;
	s->seek = buffer_seek;
	s->tell = buffer_tell;
	s->size = buffer_size;
	s->flush = buffer_flush;
	s->close = buffer_close;
	s->user_data = state;

	LOG_INF("buffer stream initialized: %zu bytes", capacity);
	return 0;
}
