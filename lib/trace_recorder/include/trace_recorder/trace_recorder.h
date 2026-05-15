/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file trace_recorder.h
 * @brief Thread trace recorder for Renode performance analysis.
 *
 * Enable with CONFIG_TRACE_RECORDER=y. The library self-registers via
 * SYS_INIT at APPLICATION 1, overriding Zephyr's __weak tracing _user
 * hooks to record thread context switches, ISR events, and idle events
 * to a static ring buffer for post-mortem dump via Renode.
 *
 * No public API.
 */

#ifndef TRACE_RECORDER_TRACE_RECORDER_H_
#define TRACE_RECORDER_TRACE_RECORDER_H_

#include <stdint.h>
#include <zephyr/kernel.h>

/* No public API — library registers itself via SYS_INIT.
 * The following symbols are exposed for the shell sub-module only. */

/** Trace record format (8 bytes) — matches Renode dump layout. */
struct trace_record {
	uint32_t timestamp; /* k_cycle_get_32() */
	uint8_t event_type; /* TRACE_EVENT_* */
	uint8_t event_data; /* priority for SWITCHED_IN, 0 otherwise */
	uint16_t thread_id; /* assigned thread ID, or 0xFFFF */
};

#ifdef __cplusplus
extern "C" {
#endif

extern struct trace_record trace_records[];
extern char trace_thread_names[][CONFIG_THREAD_MAX_NAME_LEN];
extern uint32_t g_trace_head;
extern uint32_t g_trace_overflow;

#ifdef __cplusplus
}
#endif

#endif /* TRACE_RECORDER_TRACE_RECORDER_H_ */
