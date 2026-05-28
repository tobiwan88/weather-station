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

/** Backend-specific state stored in io_stream.user_data. */
struct io_stream_flash_data {
	struct flash_img_context img_ctx;
	uint32_t partition_id;
	int32_t read_pos;
	bool initialized;
};

static ssize_t flash_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return -ENODEV;
	}

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

static ssize_t flash_write(struct io_stream *s, const uint8_t *data, size_t len)
{
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return -ENODEV;
	}

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
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return -ENODEV;
	}

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
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;
	return d->read_pos;
}

static size_t flash_size(struct io_stream *s)
{
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return 0;
	}

	/* For write streams, return bytes written. For read streams, return partition size. */
	size_t written = flash_img_bytes_written(&d->img_ctx);
	if (written > 0) {
		return written;
	}
	return d->img_ctx.flash_area->fa_size;
}

static int flash_flush(struct io_stream *s)
{
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return -ENODEV;
	}

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
	struct io_stream_flash_data *d = (struct io_stream_flash_data *)s->user_data;

	if (!d->initialized) {
		return 0; /* Already closed */
	}

	/* Flush any remaining buffered data */
	int ret = flash_img_buffered_write(&d->img_ctx, NULL, 0, true);
	if (ret < 0) {
		LOG_WRN("flash close flush failed: %d", ret);
	}

	d->read_pos = 0;
	d->initialized = false;
	LOG_DBG("flash stream closed");
	return 0;
}

static int flash_init_ctx(struct io_stream *s, uint32_t partition_id)
{
	static struct io_stream_flash_data data;
	int ret;

	if (data.initialized) {
		LOG_WRN("flash stream already initialized");
		return -EBUSY;
	}

	ret = flash_img_init_id(&data.img_ctx, (uint8_t)partition_id);
	if (ret < 0) {
		LOG_ERR("flash_img_init_id failed for partition %u: %d", partition_id, ret);
		return ret;
	}

	data.partition_id = partition_id;
	data.read_pos = 0;
	data.initialized = true;

	s->read = flash_read;
	s->write = flash_write;
	s->seek = flash_seek;
	s->tell = flash_tell;
	s->size = flash_size;
	s->flush = flash_flush;
	s->close = flash_close;
	s->user_data = &data;

	LOG_INF("flash stream initialized: partition %u, size %zu bytes", partition_id,
		data.img_ctx.flash_area->fa_size);
	return 0;
}

int io_stream_flash_init(struct io_stream *s, uint32_t partition_id)
{
	if (!s) {
		return -EINVAL;
	}
	return flash_init_ctx(s, partition_id);
}

int io_stream_flash_init_by_label(struct io_stream *s, const char *label)
{
	uint32_t partition_id;

	if (!s || !label) {
		return -EINVAL;
	}

	partition_id = (uint32_t)DT_FIXED_PARTITION_ID(DT_NODELABEL(label));
	if (partition_id == 0) {
		LOG_ERR("unknown partition label: %s", label);
		return -EINVAL;
	}

	return flash_init_ctx(s, partition_id);
}
