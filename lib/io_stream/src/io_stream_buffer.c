/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file io_stream_buffer.c
 * @brief RAM buffer stream backend.
 *
 * Wraps a caller-provided buffer with full read/write/seek/tell support.
 * Write beyond capacity returns -ENOSPC. Read past valid data returns 0 (EOF).
 *
 * Backend state is managed in an internal pool — consumers only see
 * `struct io_stream` and the vtable wrapper functions.
 *
 * Constrained by ADR-017 (Generic I/O Stream Abstraction).
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <io_stream/io_stream.h>

LOG_MODULE_REGISTER(io_stream_buffer, CONFIG_IO_STREAM_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Private state type (not exposed in header)
 * -------------------------------------------------------------------------- */

struct buffer_state {
	uint8_t *buf;
	size_t capacity;
	int32_t pos;
	size_t data_len;
	bool in_use;
};

/* --------------------------------------------------------------------------
 * Internal pool
 * -------------------------------------------------------------------------- */

#if CONFIG_IO_STREAM_BUFFER_MAX > 0
static struct buffer_state buffer_pool[CONFIG_IO_STREAM_BUFFER_MAX];
#endif

static struct buffer_state *buffer_alloc(void)
{
#if CONFIG_IO_STREAM_BUFFER_MAX > 0
	for (size_t i = 0; i < ARRAY_SIZE(buffer_pool); i++) {
		if (!buffer_pool[i].in_use) {
			buffer_pool[i].in_use = true;
			return &buffer_pool[i];
		}
	}
#endif
	return NULL;
}

/* --------------------------------------------------------------------------
 * Implementation functions (called through vtable)
 * -------------------------------------------------------------------------- */

static ssize_t buffer_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	struct buffer_state *d = (struct buffer_state *)s->context;

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
	struct buffer_state *d = (struct buffer_state *)s->context;

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
	struct buffer_state *d = (struct buffer_state *)s->context;
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
	struct buffer_state *d = (struct buffer_state *)s->context;
	return d->pos;
}

static size_t buffer_size(struct io_stream *s)
{
	struct buffer_state *d = (struct buffer_state *)s->context;
	return d->data_len;
}

static int buffer_flush(struct io_stream *s)
{
	ARG_UNUSED(s);
	return 0;
}

static int buffer_close(struct io_stream *s)
{
	struct buffer_state *d = (struct buffer_state *)s->context;
	d->pos = 0;
	d->data_len = 0;
	d->in_use = false;
	s->vtable = NULL;
	s->context = NULL;
	LOG_DBG("buffer closed");
	return 0;
}

/* --------------------------------------------------------------------------
 * Shared vtable (stored in ROM)
 * -------------------------------------------------------------------------- */

static const struct io_stream_vtable buffer_vtable = {
	.read = buffer_read,
	.write = buffer_write,
	.seek = buffer_seek,
	.tell = buffer_tell,
	.size = buffer_size,
	.flush = buffer_flush,
	.close = buffer_close,
};

/* --------------------------------------------------------------------------
 * Constructor
 * -------------------------------------------------------------------------- */

int io_stream_buffer_init(struct io_stream *s, void *buf, size_t capacity)
{
	if (!s || !buf || capacity == 0) {
		return -EINVAL;
	}
	if (s->vtable != NULL) {
		return -EBUSY;
	}

	struct buffer_state *d = buffer_alloc();
	if (!d) {
		return -ENOMEM;
	}

	d->buf = (uint8_t *)buf;
	d->capacity = capacity;
	d->pos = 0;
	d->data_len = 0;

	s->vtable = &buffer_vtable;
	s->context = d;

	LOG_INF("buffer stream initialized: %zu bytes", capacity);
	return 0;
}
