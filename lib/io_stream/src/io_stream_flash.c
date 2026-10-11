/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file io_stream_flash.c
 * @brief Flash stream backend using Zephyr's flash_img API.
 *
 * Reads use flash_area_read() at the current read position.
 * Writes use flash_img_buffered_write() for progressive erase and buffering.
 * Writes are sequential only — seeking and writing at a non-current position
 * returns -ENOTSUP.
 *
 * Backend state is managed in an internal pool — consumers only see
 * `struct io_stream` and the vtable wrapper functions.
 *
 * Constrained by ADR-017 (Generic I/O Stream Abstraction).
 */

#include <errno.h>
#include <string.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include <io_stream/io_stream.h>

LOG_MODULE_REGISTER(io_stream_flash, CONFIG_IO_STREAM_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Private state type (not exposed in header)
 * -------------------------------------------------------------------------- */

struct flash_state {
	struct flash_img_context img_ctx;
	uint32_t partition_id;
	int32_t read_pos;
	bool in_use;
};

/* --------------------------------------------------------------------------
 * Internal pool
 * -------------------------------------------------------------------------- */

#if CONFIG_IO_STREAM_FLASH_MAX > 0
static struct flash_state flash_pool[CONFIG_IO_STREAM_FLASH_MAX];
#endif

static struct flash_state *flash_alloc(void)
{
#if CONFIG_IO_STREAM_FLASH_MAX > 0
	for (size_t i = 0; i < ARRAY_SIZE(flash_pool); i++) {
		if (!flash_pool[i].in_use) {
			flash_pool[i].in_use = true;
			return &flash_pool[i];
		}
	}
#endif
	return NULL;
}

/* --------------------------------------------------------------------------
 * Implementation functions (called through vtable)
 * -------------------------------------------------------------------------- */

static ssize_t flash_stream_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	struct flash_state *d = (struct flash_state *)s->context;

	size_t partition_size = d->img_ctx.flash_area->fa_size;
	if ((size_t)d->read_pos >= partition_size) {
		return 0; /* EOF */
	}

	size_t available = partition_size - (size_t)d->read_pos;
	size_t to_read = MIN(len, available);

	int ret = flash_area_read(d->img_ctx.flash_area, d->read_pos, buf, to_read);
	if (ret < 0) {
		LOG_ERR("flash read failed at offset %d: %d", d->read_pos, ret);
		return ret;
	}

	d->read_pos += (int32_t)to_read;
	LOG_DBG("flash read: %zu bytes (pos=%d)", to_read, d->read_pos);
	return (ssize_t)to_read;
}

static ssize_t flash_stream_write(struct io_stream *s, const uint8_t *data, size_t len)
{
	struct flash_state *d = (struct flash_state *)s->context;

	/* flash_img only supports sequential writes. If the read cursor has
	 * been seeked ahead of the write position, reject the write.
	 */
	size_t written = flash_img_bytes_written(&d->img_ctx);
	if ((size_t)d->read_pos != written) {
		LOG_WRN("flash write at non-sequential position (pos=%d, written=%zu)", d->read_pos,
			written);
		return -ENOTSUP;
	}

	int ret = flash_img_buffered_write(&d->img_ctx, data, len, false);
	if (ret < 0) {
		LOG_ERR("flash write failed at offset %zu: %d", written, ret);
		return ret;
	}

	d->read_pos = (int32_t)flash_img_bytes_written(&d->img_ctx);
	LOG_DBG("flash write: %zu bytes (pos=%d)", len, d->read_pos);
	return (ssize_t)len;
}

static int flash_seek(struct io_stream *s, int32_t offset, int whence)
{
	struct flash_state *d = (struct flash_state *)s->context;

	size_t partition_size = d->img_ctx.flash_area->fa_size;
	int32_t new_pos;

	switch (whence) {
	case IO_STREAM_SEEK_SET:
		new_pos = offset;
		break;
	case IO_STREAM_SEEK_CUR:
		new_pos = d->read_pos + offset;
		break;
	case IO_STREAM_SEEK_END:
		new_pos = (int32_t)partition_size + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_pos < 0 || (size_t)new_pos > partition_size) {
		return -EINVAL;
	}

	d->read_pos = new_pos;
	LOG_DBG("flash seek: pos=%d (whence=%d, offset=%d)", d->read_pos, whence, offset);
	return 0;
}

static int32_t flash_tell(struct io_stream *s)
{
	struct flash_state *d = (struct flash_state *)s->context;
	return d->read_pos;
}

static size_t flash_size(struct io_stream *s)
{
	struct flash_state *d = (struct flash_state *)s->context;

	/* For write streams, return bytes written. For read streams, return partition size. */
	size_t written = flash_img_bytes_written(&d->img_ctx);
	if (written > 0) {
		return written;
	}
	return d->img_ctx.flash_area->fa_size;
}

static int flash_flush(struct io_stream *s)
{
	struct flash_state *d = (struct flash_state *)s->context;

	int ret = flash_img_buffered_write(&d->img_ctx, NULL, 0, true);
	if (ret < 0) {
		LOG_ERR("flash flush failed: %d", ret);
		return ret;
	}

	d->read_pos = (int32_t)flash_img_bytes_written(&d->img_ctx);
	LOG_DBG("flash flushed: %zu bytes written", flash_img_bytes_written(&d->img_ctx));
	return 0;
}

static int flash_close(struct io_stream *s)
{
	struct flash_state *d = (struct flash_state *)s->context;
	if (!d || !d->in_use) {
		return 0;
	}

	/* Flush any remaining buffered data */
	int ret = flash_img_buffered_write(&d->img_ctx, NULL, 0, true);
	if (ret < 0) {
		LOG_WRN("flash close flush failed: %d", ret);
	}

	d->read_pos = 0;
	d->in_use = false;
	s->vtable = NULL;
	s->context = NULL;
	LOG_DBG("flash stream closed");
	return 0;
}

/* --------------------------------------------------------------------------
 * Shared vtable (stored in ROM)
 * -------------------------------------------------------------------------- */

static const struct io_stream_vtable flash_vtable = {
	.read = flash_stream_read,
	.write = flash_stream_write,
	.seek = flash_seek,
	.tell = flash_tell,
	.size = flash_size,
	.flush = flash_flush,
	.close = flash_close,
};

/* --------------------------------------------------------------------------
 * Internal init helper
 * -------------------------------------------------------------------------- */

static int flash_init_ctx(struct io_stream *s, uint32_t partition_id)
{
	if (!s) {
		return -EINVAL;
	}
	if (s->vtable != NULL) {
		return -EBUSY;
	}

	struct flash_state *d = flash_alloc();
	if (!d) {
		return -ENOMEM;
	}

	int ret = flash_img_init_id(&d->img_ctx, (uint8_t)partition_id);
	if (ret < 0) {
		d->in_use = false;
		LOG_ERR("flash_img_init_id failed for partition %u: %d", partition_id, ret);
		return ret;
	}

	d->partition_id = partition_id;
	d->read_pos = 0;

	s->vtable = &flash_vtable;
	s->context = d;

	LOG_INF("flash stream initialized: partition %u, size %zu bytes", partition_id,
		d->img_ctx.flash_area->fa_size);
	return 0;
}

/* --------------------------------------------------------------------------
 * Public constructors
 * -------------------------------------------------------------------------- */

int io_stream_flash_init(struct io_stream *s, uint32_t partition_id)
{
	return flash_init_ctx(s, partition_id);
}
