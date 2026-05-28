/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/io_stream)
 * @brief Unit tests for io_stream buffer backend and vtable helpers.
 */

#include <errno.h>
#include <io_stream/io_stream.h>
#include <string.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(io_stream_suite, NULL, NULL, NULL, NULL, NULL);

/* --------------------------------------------------------------------------
 * Buffer stream tests
 * -------------------------------------------------------------------------- */

/**
 * @brief Write data and read it back sequentially.
 */
ZTEST(io_stream_suite, test_buffer_write_then_read)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t test_data[] = "Hello, io_stream!";
	uint8_t read_buf[32];

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, test_data, sizeof(test_data) - 1);
	zassert_equal(written, sizeof(test_data) - 1, "write returned %zd", written);

	ret = io_stream_seek(&stream, 0, IO_STREAM_SEEK_SET);
	zassert_equal(ret, 0, "seek failed: %d", ret);

	ssize_t read = io_stream_read(&stream, read_buf, sizeof(read_buf));
	zassert_equal(read, sizeof(test_data) - 1, "read returned %zd", read);
	zassert_mem_equal(read_buf, test_data, sizeof(test_data) - 1, "data mismatch");
}

/**
 * @brief SEEK_SET positions cursor at absolute offset.
 */
ZTEST(io_stream_suite, test_buffer_seek_set)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "0123456789";

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, data, sizeof(data));
	zassert_equal(written, sizeof(data), "write failed");

	ret = io_stream_seek(&stream, 5, IO_STREAM_SEEK_SET);
	zassert_equal(ret, 0, "seek failed: %d", ret);

	int32_t pos = io_stream_tell(&stream);
	zassert_equal(pos, 5, "tell returned %d, expected 5", pos);

	uint8_t ch;
	ssize_t read = io_stream_read(&stream, &ch, 1);
	zassert_equal(read, 1, "read failed");
	zassert_equal(ch, '5', "read '0x%02x', expected '5'", ch);
}

/**
 * @brief SEEK_CUR positions cursor relative to current position.
 */
ZTEST(io_stream_suite, test_buffer_seek_cur)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "ABCDEFGHIJ";

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, data, sizeof(data));
	zassert_equal(written, sizeof(data), "write failed");

	ret = io_stream_seek(&stream, 0, IO_STREAM_SEEK_SET);
	zassert_equal(ret, 0, "seek to 0 failed");

	ret = io_stream_seek(&stream, 3, IO_STREAM_SEEK_CUR);
	zassert_equal(ret, 0, "seek cur +3 failed");

	int32_t pos = io_stream_tell(&stream);
	zassert_equal(pos, 3, "tell returned %d, expected 3", pos);

	uint8_t ch;
	ssize_t read = io_stream_read(&stream, &ch, 1);
	zassert_equal(read, 1, "read failed");
	zassert_equal(ch, 'D', "read '0x%02x', expected 'D'", ch);
}

/**
 * @brief SEEK_END positions cursor relative to end of valid data.
 */
ZTEST(io_stream_suite, test_buffer_seek_end)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "0123456789"; /* 11 bytes including null terminator */

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, data, sizeof(data));
	zassert_equal(written, sizeof(data), "write failed");

	ret = io_stream_seek(&stream, -3, IO_STREAM_SEEK_END);
	zassert_equal(ret, 0, "seek end -3 failed: %d", ret);

	int32_t pos = io_stream_tell(&stream);
	/* data_len = 11, seek(-3, END) = 11 - 3 = 8 */
	zassert_equal(pos, 8, "tell returned %d, expected 8", pos);
}

/**
 * @brief Read past end of valid data returns 0 (EOF).
 */
ZTEST(io_stream_suite, test_buffer_read_eof)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "ABC"; /* 4 bytes including null terminator */

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, data, sizeof(data));
	zassert_equal(written, sizeof(data), "write failed");

	ret = io_stream_seek(&stream, 0, IO_STREAM_SEEK_SET);
	zassert_equal(ret, 0, "seek failed");

	uint8_t read_buf[10];
	ssize_t read = io_stream_read(&stream, read_buf, sizeof(read_buf));
	zassert_equal(read, sizeof(data), "read returned %zd, expected %zu", read, sizeof(data));

	read = io_stream_read(&stream, read_buf, sizeof(read_buf));
	zassert_equal(read, 0, "read past EOF returned %zd, expected 0", read);
}

/**
 * @brief Write beyond buffer capacity returns -ENOSPC.
 */
ZTEST(io_stream_suite, test_buffer_write_full)
{
	uint8_t buf[8];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[16] = {0};

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ssize_t written = io_stream_write(&stream, data, sizeof(buf));
	zassert_equal(written, sizeof(buf), "write full buffer failed");

	written = io_stream_write(&stream, data, 1);
	zassert_equal(written, -ENOSPC, "write past capacity returned %zd, expected -ENOSPC",
		      written);
}

/**
 * @brief Invalid seek offset returns -EINVAL.
 */
ZTEST(io_stream_suite, test_buffer_seek_invalid)
{
	uint8_t buf[16];
	struct io_stream_buffer_state state;
	struct io_stream stream;

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ret = io_stream_seek(&stream, -1, IO_STREAM_SEEK_SET);
	zassert_equal(ret, -EINVAL, "negative SEEK_SET returned %d, expected -EINVAL", ret);

	ret = io_stream_seek(&stream, 100, IO_STREAM_SEEK_SET);
	zassert_equal(ret, -EINVAL, "beyond capacity SEEK_SET returned %d, expected -EINVAL", ret);

	ret = io_stream_seek(&stream, 0, 99);
	zassert_equal(ret, -EINVAL, "invalid whence returned %d, expected -EINVAL", ret);
}

/**
 * @brief size() returns the number of bytes written.
 */
ZTEST(io_stream_suite, test_buffer_size)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "12345";

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	zassert_equal(io_stream_size(&stream), 0, "initial size should be 0");

	ssize_t written = io_stream_write(&stream, data, sizeof(data));
	zassert_equal(written, sizeof(data), "write failed");

	zassert_equal(io_stream_size(&stream), sizeof(data), "size returned %zu, expected %zu",
		      io_stream_size(&stream), sizeof(data));
}

/**
 * @brief close() resets position and data length.
 */
ZTEST(io_stream_suite, test_buffer_close)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;
	const uint8_t data[] = "test";

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	io_stream_write(&stream, data, sizeof(data));

	ret = io_stream_close(&stream);
	zassert_equal(ret, 0, "close failed: %d", ret);

	zassert_equal(io_stream_tell(&stream), 0, "tell after close should be 0");
	zassert_equal(io_stream_size(&stream), 0, "size after close should be 0");
}

/**
 * @brief flush() is a no-op for buffer streams.
 */
ZTEST(io_stream_suite, test_buffer_flush)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	ret = io_stream_flush(&stream);
	zassert_equal(ret, 0, "flush failed: %d", ret);
}

/**
 * @brief Null stream returns -ENOSYS from wrapper helpers.
 */
ZTEST(io_stream_suite, test_null_stream)
{
	uint8_t buf[8];

	zassert_equal(io_stream_read(NULL, buf, sizeof(buf)), -ENOSYS,
		      "null read should return -ENOSYS");
	zassert_equal(io_stream_write(NULL, buf, sizeof(buf)), -ENOSYS,
		      "null write should return -ENOSYS");
	zassert_equal(io_stream_seek(NULL, 0, IO_STREAM_SEEK_SET), -ENOSYS,
		      "null seek should return -ENOSYS");
	zassert_equal(io_stream_tell(NULL), -ENOSYS, "null tell should return -ENOSYS");
	zassert_equal(io_stream_size(NULL), 0, "null size should return 0");
	zassert_equal(io_stream_flush(NULL), 0, "null flush should return 0");
	zassert_equal(io_stream_close(NULL), 0, "null close should return 0");
}

/**
 * @brief Stream with null vtable entries returns -ENOSYS/-ENOSYS/0.
 */
ZTEST(io_stream_suite, test_null_vtable)
{
	struct io_stream stream = {0};
	uint8_t buf[8];

	zassert_equal(io_stream_read(&stream, buf, sizeof(buf)), -ENOSYS,
		      "null read fn should return -ENOSYS");
	zassert_equal(io_stream_write(&stream, buf, sizeof(buf)), -ENOSYS,
		      "null write fn should return -ENOSYS");
	zassert_equal(io_stream_seek(&stream, 0, IO_STREAM_SEEK_SET), -ENOSYS,
		      "null seek fn should return -ENOSYS");
	zassert_equal(io_stream_tell(&stream), -ENOSYS, "null tell fn should return -ENOSYS");
	zassert_equal(io_stream_size(&stream), 0, "null size fn should return 0");
	zassert_equal(io_stream_flush(&stream), 0, "null flush fn should return 0");
	zassert_equal(io_stream_close(&stream), 0, "null close fn should return 0");
}

/**
 * @brief Multiple write-read cycles with seek in between.
 */
ZTEST(io_stream_suite, test_buffer_interleaved_rw)
{
	uint8_t buf[64];
	struct io_stream_buffer_state state;
	struct io_stream stream;

	int ret = io_stream_buffer_init(&stream, &state, buf, sizeof(buf));
	zassert_equal(ret, 0, "init failed: %d", ret);

	const uint8_t data1[] = "AAAAA";
	const uint8_t data2[] = "BBBBB";

	ssize_t w1 = io_stream_write(&stream, data1, sizeof(data1) - 1);
	zassert_equal(w1, 5, "first write failed");

	ssize_t w2 = io_stream_write(&stream, data2, sizeof(data2) - 1);
	zassert_equal(w2, 5, "second write failed");

	zassert_equal(io_stream_size(&stream), 10, "size should be 10");

	ret = io_stream_seek(&stream, 0, IO_STREAM_SEEK_SET);
	zassert_equal(ret, 0, "seek failed");

	uint8_t read_buf[16];
	ssize_t r = io_stream_read(&stream, read_buf, 10);
	zassert_equal(r, 10, "read failed");

	zassert_mem_equal(read_buf, "AAAAABBBBB", 10, "interleaved data mismatch");
}
