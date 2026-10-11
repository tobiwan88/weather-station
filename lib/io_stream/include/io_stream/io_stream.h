/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file io_stream.h
 * @brief Generic I/O stream abstraction with pluggable backends.
 *
 * Provides a Unix-style vtable interface for sequential read/write operations
 * with position tracking (seek, tell). Backends include flash (wrapping
 * Zephyr's flash_img) and RAM buffer (for native_sim testing).
 *
 * All stream instances are statically allocated (no heap).
 * Backend state is managed internally by each backend — consumers only
 * interact with `struct io_stream` and the vtable wrapper functions.
 *
 * Constrained by ADR-017 (Generic I/O Stream Abstraction).
 */

#ifndef IO_STREAM_IO_STREAM_H_
#define IO_STREAM_IO_STREAM_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Seek whence constants (matching POSIX semantics)
 * -------------------------------------------------------------------------- */

/** Seek relative to beginning of stream. */
#define IO_STREAM_SEEK_SET 0
/** Seek relative to current position. */
#define IO_STREAM_SEEK_CUR 1
/** Seek relative to end of stream. */
#define IO_STREAM_SEEK_END 2

/* --------------------------------------------------------------------------
 * Stream vtable
 * -------------------------------------------------------------------------- */

struct io_stream;

/**
 * @brief Shared vtable for io_stream backends.
 *
 * Each backend defines one static const instance of this struct (stored in ROM).
 * All instances of a given backend share the same vtable pointer.
 */
struct io_stream_vtable {
	ssize_t (*read)(struct io_stream *s, uint8_t *buf, size_t len);
	ssize_t (*write)(struct io_stream *s, const uint8_t *data, size_t len);
	int (*seek)(struct io_stream *s, int32_t offset, int whence);
	int32_t (*tell)(struct io_stream *s);
	size_t (*size)(struct io_stream *s);
	int (*flush)(struct io_stream *s);
	int (*close)(struct io_stream *s);
};

/**
 * @brief Generic I/O stream instance.
 *
 * Backend state is opaque — managed internally by the constructor.
 * Consumers only use the vtable wrapper functions below.
 */
struct io_stream {
	const struct io_stream_vtable *vtable;
	void *context;
};

/* --------------------------------------------------------------------------
 * Wrapper helpers (handle null stream / null vtable / null fn pointers)
 * -------------------------------------------------------------------------- */

/**
 * @brief Read data from the stream at the current position.
 *
 * Returns -ENOSYS if the stream or vtable is null, or if read is not
 * implemented. Returns 0 on EOF.
 */
static inline ssize_t io_stream_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	if (!s || !s->vtable || !s->vtable->read) {
		return -ENOSYS;
	}
	return s->vtable->read(s, buf, len);
}

/**
 * @brief Write data to the stream at the current position.
 *
 * Returns -ENOSYS if the stream or vtable is null, or if write is not
 * implemented.
 */
static inline ssize_t io_stream_write(struct io_stream *s, const uint8_t *data, size_t len)
{
	if (!s || !s->vtable || !s->vtable->write) {
		return -ENOSYS;
	}
	return s->vtable->write(s, data, len);
}

/**
 * @brief Reposition the stream's internal cursor.
 *
 * Returns -ENOSYS if the stream or vtable is null, or if seek is not
 * implemented.
 */
static inline int io_stream_seek(struct io_stream *s, int32_t offset, int whence)
{
	if (!s || !s->vtable || !s->vtable->seek) {
		return -ENOSYS;
	}
	return s->vtable->seek(s, offset, whence);
}

/**
 * @brief Return the current cursor position.
 *
 * Returns -ENOSYS if the stream or vtable is null, or if tell is not
 * implemented.
 */
static inline int32_t io_stream_tell(struct io_stream *s)
{
	if (!s || !s->vtable || !s->vtable->tell) {
		return -ENOSYS;
	}
	return s->vtable->tell(s);
}

/**
 * @brief Return the total size of valid data in the stream.
 *
 * Returns 0 if the stream or vtable is null, or if size is not
 * implemented.
 */
static inline size_t io_stream_size(struct io_stream *s)
{
	if (!s || !s->vtable || !s->vtable->size) {
		return 0;
	}
	return s->vtable->size(s);
}

/**
 * @brief Flush any buffered data to the underlying backend.
 *
 * Returns 0 if the stream or vtable is null, or if flush is not
 * implemented (no-op).
 */
static inline int io_stream_flush(struct io_stream *s)
{
	if (!s || !s->vtable || !s->vtable->flush) {
		return 0;
	}
	return s->vtable->flush(s);
}

/**
 * @brief Close the stream and release backend resources.
 *
 * Returns 0 if the stream or vtable is null, or if close is not
 * implemented (no-op).
 */
static inline int io_stream_close(struct io_stream *s)
{
	if (!s || !s->vtable || !s->vtable->close) {
		return 0;
	}
	return s->vtable->close(s);
}

/* --------------------------------------------------------------------------
 * Backend constructors
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_IO_STREAM_BUFFER)
/**
 * @brief Initialize a RAM buffer stream.
 *
 * The caller provides the raw buffer memory. Position/length tracking
 * state is managed internally by the backend pool.
 *
 * @param s        Stream instance to initialize.
 * @param buf      Caller-provided buffer memory.
 * @param capacity Buffer size in bytes.
 * @return 0 on success, negative errno on error.
 */
int io_stream_buffer_init(struct io_stream *s, void *buf, size_t capacity);
#endif

#if defined(CONFIG_IO_STREAM_FLASH)
/**
 * @brief Initialize a flash stream by partition ID.
 *
 * Opens the specified flash partition and prepares it for read/write.
 * Uses flash_img internally for buffered writes with progressive erase.
 *
 * @param s            Stream instance to initialize.
 * @param partition_id Flash partition ID (from FIXED_PARTITION_ID).
 * @return 0 on success, negative errno on error.
 */
int io_stream_flash_init(struct io_stream *s, uint32_t partition_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IO_STREAM_IO_STREAM_H_ */
