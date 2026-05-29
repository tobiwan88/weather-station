/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file io_stream.c
 * @brief I/O stream core — logging registration and no-op init.
 *
 * The io_stream library provides types and functions; streams are initialized
 * on demand by callers. This file exists for LOG_MODULE_REGISTER and to
 * reserve an init priority slot for future global stream registry.
 *
 * Constrained by ADR-017 (Generic I/O Stream Abstraction).
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <io_stream/io_stream.h>

LOG_MODULE_REGISTER(io_stream, CONFIG_IO_STREAM_LOG_LEVEL);

static int io_stream_init(void)
{
	LOG_DBG("io_stream: initialized");
	return 0;
}

SYS_INIT(io_stream_init, APPLICATION, 80);
