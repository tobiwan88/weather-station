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

/**
 * @brief Generic I/O stream interface.
 *
 * Each backend initializes the function pointers and user_data.
 * Callers invoke operations through the vtable — the backend handles
 * all implementation details.
 *
 * Position tracking is stateful: read() and write() advance the internal
 * cursor. seek() repositions it. tell() returns the current position.
 *
 * Unsupported operations return -ENOTSUP. Null function pointers are
 * treated as -ENOSYS by wrapper helpers.
 */
struct io_stream {
	/**
	 * @brief Read data from the stream at the current position.
	 *
	 * Advances the position by the number of bytes actually read.
	 * Returns 0 on EOF (no more data available).
	 *
	 * @param s   Stream instance.
	 * @param buf Destination buffer.
	 * @param len Maximum bytes to read.
	 * @return Bytes read (>0), 0 on EOF, or negative errno on error.
	 */
	ssize_t (*read)(struct io_stream *s, uint8_t *buf, size_t len);

	/**
	 * @brief Write data to the stream at the current position.
	 *
	 * Advances the position by the number of bytes actually written.
	 * For flash backends, writes are sequential only (seek + write
	 * at non-current position returns -ENOTSUP).
	 *
	 * @param s    Stream instance.
	 * @param data Source data.
	 * @param len  Number of bytes to write.
	 * @return Bytes written (>0), or negative errno on error.
	 */
	ssize_t (*write)(struct io_stream *s, const uint8_t *data, size_t len);

	/**
	 * @brief Reposition the stream's internal cursor.
	 *
	 * @param s      Stream instance.
	 * @param offset Byte offset.
	 * @param whence IO_STREAM_SEEK_SET, IO_STREAM_SEEK_CUR, or IO_STREAM_SEEK_END.
	 * @return 0 on success, negative errno on error.
	 */
	int (*seek)(struct io_stream *s, int32_t offset, int whence);

	/**
	 * @brief Return the current cursor position.
	 *
	 * @param s Stream instance.
	 * @return Current position (>=0), or negative errno on error.
	 */
	int32_t (*tell)(struct io_stream *s);

	/**
	 * @brief Return the total size of valid data in the stream.
	 *
	 * For read streams: total readable bytes.
	 * For write streams: bytes written so far.
	 *
	 * @param s Stream instance.
	 * @return Total size, or 0 if unknown.
	 */
	size_t (*size)(struct io_stream *s);

	/**
	 * @brief Flush any buffered data to the underlying backend.
	 *
	 * For flash backends: commits the flash_img write buffer.
	 * For buffer backends: no-op (data is already in memory).
	 *
	 * @param s Stream instance.
	 * @return 0 on success, negative errno on error.
	 */
	int (*flush)(struct io_stream *s);

	/**
	 * @brief Close the stream and release backend resources.
	 *
	 * The stream struct itself is not freed (static allocation).
	 * After close, the stream may be re-initialized by the backend.
	 *
	 * @param s Stream instance.
	 * @return 0 on success, negative errno on error.
	 */
	int (*close)(struct io_stream *s);

	/** Opaque backend-specific state (position, flash_img_context, etc.). */
	void *user_data;
};

/* --------------------------------------------------------------------------
 * Wrapper helpers (handle null function pointers)
 * -------------------------------------------------------------------------- */

/**
 * @brief Read through the stream vtable.
 *
 * Returns -ENOSYS if the backend does not implement read.
 */
static inline ssize_t io_stream_read(struct io_stream *s, uint8_t *buf, size_t len)
{
	if (!s || !s->read) {
		return -ENOSYS;
	}
	return s->read(s, buf, len);
}

/**
 * @brief Write through the stream vtable.
 *
 * Returns -ENOSYS if the backend does not implement write.
 */
static inline ssize_t io_stream_write(struct io_stream *s, const uint8_t *data, size_t len)
{
	if (!s || !s->write) {
		return -ENOSYS;
	}
	return s->write(s, data, len);
}

/**
 * @brief Seek through the stream vtable.
 *
 * Returns -ENOSYS if the backend does not implement seek.
 */
static inline int io_stream_seek(struct io_stream *s, int32_t offset, int whence)
{
	if (!s || !s->seek) {
		return -ENOSYS;
	}
	return s->seek(s, offset, whence);
}

/**
 * @brief Tell through the stream vtable.
 *
 * Returns -ENOSYS if the backend does not implement tell.
 */
static inline int32_t io_stream_tell(struct io_stream *s)
{
	if (!s || !s->tell) {
		return -ENOSYS;
	}
	return s->tell(s);
}

/**
 * @brief Size through the stream vtable.
 *
 * Returns 0 if the backend does not implement size.
 */
static inline size_t io_stream_size(struct io_stream *s)
{
	if (!s || !s->size) {
		return 0;
	}
	return s->size(s);
}

/**
 * @brief Flush through the stream vtable.
 *
 * Returns 0 if the backend does not implement flush (no-op).
 */
static inline int io_stream_flush(struct io_stream *s)
{
	if (!s || !s->flush) {
		return 0;
	}
	return s->flush(s);
}

/**
 * @brief Close through the stream vtable.
 *
 * Returns 0 if the backend does not implement close (no-op).
 */
static inline int io_stream_close(struct io_stream *s)
{
	if (!s || !s->close) {
		return 0;
	}
	return s->close(s);
}

/* --------------------------------------------------------------------------
 * Backend initialization functions
 * -------------------------------------------------------------------------- */

#if defined(CONFIG_IO_STREAM_BUFFER)
/**
 * @brief Opaque state for a buffer stream instance.
 *
 * Caller allocates this (stack, BSS, or static) and passes it to
 * io_stream_buffer_init(). No heap allocation.
 */
struct io_stream_buffer_state {
	uint8_t *buf;
	size_t capacity;
	int32_t pos;
	size_t data_len;
};

/**
 * @brief Initialize a RAM buffer stream.
 *
 * The caller provides both the buffer memory and a state struct.
 * The stream tracks valid data length separately from buffer capacity.
 *
 * @param s        Stream instance to initialize.
 * @param state    Caller-provided state struct (stack/BSS/static).
 * @param buf      Caller-provided buffer memory.
 * @param capacity Buffer size in bytes.
 * @return 0 on success, negative errno on error.
 */
int io_stream_buffer_init(struct io_stream *s, struct io_stream_buffer_state *state, void *buf,
			  size_t capacity);
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

/**
 * @brief Initialize a flash stream by DTS label.
 *
 * Convenience wrapper that resolves the partition ID from a DTS label.
 *
 * @param s     Stream instance to initialize.
 * @param label DTS partition label (e.g., "image-1").
 * @return 0 on success, negative errno on error.
 */
int io_stream_flash_init_by_label(struct io_stream *s, const char *label);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IO_STREAM_IO_STREAM_H_ */
