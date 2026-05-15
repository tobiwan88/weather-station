/* SPDX-License-Identifier: Apache-2.0 */
#include <tracing_user.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <trace_recorder/trace_recorder.h>

ZTEST_SUITE(trace_recorder_suite, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Verify the trace record struct is exactly 8 bytes.
 */
ZTEST(trace_recorder_suite, test_record_size)
{
	zassert_equal(sizeof(struct trace_record), 8, "trace_record must be 8 bytes");
}

/**
 * @brief Verify initial state after boot.
 */
ZTEST(trace_recorder_suite, test_initial_state)
{
	/* trace_ready is set by SYS_INIT, so hooks should be active */
	/* trace_head should be > 0 from boot-time thread activity */
	zassert_true(trace_head > 0, "trace_head should be > 0 after boot");
}

/**
 * @brief Verify ring buffer wraps when it fills.
 * Uses a small buffer (16 records) configured via Kconfig.
 */
ZTEST(trace_recorder_suite, test_ring_buffer_wrap)
{
	uint32_t overflow_before = trace_overflow;

	/* Generate enough events to fill the buffer.
	 * Each switched_in/out pair writes 2 records.
	 * With a 16-record buffer, 8 pairs should fill it. */
	for (int i = 0; i < 20; i++) {
		sys_trace_thread_switched_in_user();
		sys_trace_thread_switched_out_user();
	}

	/* Buffer should have wrapped */
	zassert_true(trace_overflow > overflow_before,
		     "trace_overflow should increase after buffer fills");
	/* Head should be somewhere in the buffer (wrapped) */
	zassert_true(trace_head < CONFIG_TRACE_RECORDER_BUFFER_SIZE,
		     "trace_head should be within buffer bounds after wrap");
}

/**
 * @brief Verify ISR events are recorded with UNKNOWN thread ID.
 */
ZTEST(trace_recorder_suite, test_isr_events)
{
	uint32_t head_before = trace_head;

	sys_trace_isr_enter_user();
	sys_trace_isr_exit_user();

	zassert_equal(trace_head, head_before + 2, "ISR enter/exit should write 2 records");

	/* Verify the last two records are ISR events with UNKNOWN thread ID */
	struct trace_record *enter =
		&trace_records[(head_before) % CONFIG_TRACE_RECORDER_BUFFER_SIZE];
	struct trace_record *exit =
		&trace_records[(head_before + 1) % CONFIG_TRACE_RECORDER_BUFFER_SIZE];

	zassert_equal(enter->event_type, 3, "ISR enter event type should be 3");
	zassert_equal(enter->thread_id, 0xFFFF, "ISR enter thread_id should be 0xFFFF");
	zassert_equal(exit->event_type, 4, "ISR exit event type should be 4");
	zassert_equal(exit->thread_id, 0xFFFF, "ISR exit thread_id should be 0xFFFF");
}

/**
 * @brief Verify idle events are recorded.
 */
ZTEST(trace_recorder_suite, test_idle_event)
{
	uint32_t head_before = trace_head;

	sys_trace_idle_user();

	zassert_equal(trace_head, (head_before + 1) % CONFIG_TRACE_RECORDER_BUFFER_SIZE,
		      "idle event should write 1 record");

	struct trace_record *rec = &trace_records[head_before];
	zassert_equal(rec->event_type, 5, "idle event type should be 5");
	zassert_equal(rec->thread_id, 0xFFFF, "idle thread_id should be 0xFFFF");
}

/**
 * @brief Verify switched_in records priority in event_data.
 */
ZTEST(trace_recorder_suite, test_switched_in_priority)
{
	uint32_t head_before = trace_head;

	/* The current thread's priority should be recorded */
	sys_trace_thread_switched_in_user();

	struct trace_record *rec = &trace_records[head_before];
	zassert_equal(rec->event_type, 0, "switched_in event type should be 0");
	/* event_data should contain the current thread's priority */
	int8_t expected_prio = (int8_t)k_thread_priority_get(k_current_get());
	zassert_equal(rec->event_data, (uint8_t)expected_prio,
		      "event_data should contain thread priority");
}
