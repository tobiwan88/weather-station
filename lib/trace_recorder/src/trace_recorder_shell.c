/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file trace_recorder_shell.c
 * @brief Shell commands for the trace recorder.
 *
 *   trace status   — buffer utilisation / overflow count
 *   trace dump     — hex dump of records + thread name table
 *   trace clear    — reset buffer
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <trace_recorder/trace_recorder.h>

/* ── trace status ─────────────────────────────────────────────────── */

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_spinlock_key_t key = k_spin_lock(&trace_lock);
	uint32_t head = trace_head;
	uint32_t overflow = trace_overflow;
	k_spin_unlock(&trace_lock, key);

	uint32_t capacity = CONFIG_TRACE_RECORDER_BUFFER_SIZE;
	uint32_t valid_count = overflow ? capacity : head;

	shell_print(sh, "Trace recorder status:");
	shell_print(sh, "  Records:    %u / %u (%u%%)", valid_count, capacity,
		    capacity ? (unsigned)(valid_count * 100ULL / capacity) : 0);
	shell_print(sh, "  Overflow:   %u wraps", overflow);
	shell_print(sh, "  Each record: 8 bytes (total buffer: %u bytes)", capacity * 8);

	return 0;
}

/* ── trace dump ───────────────────────────────────────────────────── */

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_spinlock_key_t key = k_spin_lock(&trace_lock);
	uint32_t head = trace_head;
	uint32_t overflow = trace_overflow;
	uint32_t capacity = CONFIG_TRACE_RECORDER_BUFFER_SIZE;
	uint32_t valid_count = overflow ? capacity : head;

	shell_print(sh, "--- TRACE DUMP BEGIN ---");
	shell_print(sh, "records=%u capacity=%u overflow=%u", valid_count, capacity, overflow);

	/* print thread name table */
	shell_print(sh, "thread_names:");
	for (uint32_t i = 0; i < CONFIG_TRACE_RECORDER_MAX_THREADS; i++) {
		if (trace_thread_names[i][0] != '\0') {
			shell_print(sh, "  [%u] %s", i, trace_thread_names[i]);
		}
	}

	/* print records as hex (8 bytes each, 16 bytes per shell line).
	 * Read one record at a time under the lock to avoid a large
	 * stack allocation. */
	shell_print(sh, "records_hex:");
	for (uint32_t i = 0; i < valid_count; i += 2) {
		struct trace_record r0 = trace_records[i];
		struct trace_record r1 = {0};
		if (i + 1 < valid_count) {
			r1 = trace_records[i + 1];
		}
		k_spin_unlock(&trace_lock, key);

		const uint8_t *b0 = (const uint8_t *)&r0;
		const uint8_t *b1 = (const uint8_t *)&r1;
		uint32_t remain = (i + 1 < valid_count) ? 16 : 8;

		if (remain >= 16) {
			shell_print(sh,
				    "%02x%02x%02x%02x%02x%02x%02x%02x"
				    "%02x%02x%02x%02x%02x%02x%02x%02x",
				    b0[0], b0[1], b0[2], b0[3], b0[4], b0[5], b0[6], b0[7], b1[0],
				    b1[1], b1[2], b1[3], b1[4], b1[5], b1[6], b1[7]);
		} else {
			char buf[128];
			int off = 0;
			for (uint32_t j = 0; j < remain; j++) {
				off += snprintf(buf + off, sizeof(buf) - off, "%02x", b0[j]);
			}
			shell_print(sh, "%s", buf);
		}

		if (i + 1 < valid_count) {
			key = k_spin_lock(&trace_lock);
		}
	}

	shell_print(sh, "--- TRACE DUMP END ---");
	return 0;
}

/* ── trace clear ──────────────────────────────────────────────────── */

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_spinlock_key_t key = k_spin_lock(&trace_lock);
	trace_head = 0;
	trace_overflow = 0;
	memset(trace_thread_names, 0,
	       CONFIG_TRACE_RECORDER_MAX_THREADS * CONFIG_THREAD_MAX_NAME_LEN);
	k_spin_unlock(&trace_lock, key);

	shell_print(sh, "Trace buffer cleared.");
	return 0;
}

/* ── command registration ─────────────────────────────────────────── */

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_trace, SHELL_CMD(status, NULL, "Show trace buffer utilisation", cmd_status),
	SHELL_CMD(dump, NULL, "Hex dump of trace records + thread names", cmd_dump),
	SHELL_CMD(clear, NULL, "Reset trace buffer", cmd_clear), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(trace, &sub_trace, "Trace recorder controls", NULL);
