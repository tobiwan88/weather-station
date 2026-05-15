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

/* No public API — library registers itself via SYS_INIT. */

#endif /* TRACE_RECORDER_TRACE_RECORDER_H_ */
