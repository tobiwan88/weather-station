# Reduce Stack Usage in trace dump Command — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the 32 KB stack allocation risk in `cmd_dump` by allowing `k_malloc` for this test-only shell command, gated behind a Kconfig option.

**Architecture:** Add `CONFIG_TRACE_RECORDER_CMD_DUMP_MALLOC` Kconfig that, when enabled, uses `k_malloc`/`k_free` in `cmd_dump` instead of the current per-record iteration approach. This is only for the shell command (test/debug use), not the core tracing hooks which remain heap-free.

**Tech Stack:** Zephyr `k_malloc`/`k_free`, Kconfig composition, shell subsystem.

---

### Task 1: Add Kconfig option for malloc-based dump

**Files:**
- Modify: `lib/trace_recorder/Kconfig`

- [ ] **Step 1: Add Kconfig option**

Add after the existing `TRACE_RECORDER_SHELL` config in `lib/trace_recorder/Kconfig`:

```kconfig
config TRACE_RECORDER_CMD_DUMP_MALLOC
	bool "Use heap allocation for trace dump command"
	default n
	depends on TRACE_RECORDER_SHELL
	depends on HEAP_MEM_POOL_SIZE > 0
	help
	  When enabled, the 'trace dump' shell command allocates a snapshot
	  buffer via k_malloc instead of iterating records one at a time.
	  This reduces shell thread stack usage at the cost of heap usage.

	  Only enable for testing/debugging — not recommended for production
	  builds where heap is constrained or unavailable.

	  If k_malloc fails, falls back to per-record iteration.
```

- [ ] **Step 2: Commit**

```bash
git add lib/trace_recorder/Kconfig
git commit -m "feat(trace_recorder): add CMD_DUMP_MALLOC Kconfig for heap-based dump"
```

---

### Task 2: Update cmd_dump to use k_malloc when enabled

**Files:**
- Modify: `lib/trace_recorder/src/trace_recorder_shell.c`

- [ ] **Step 1: Update cmd_dump**

Replace the current `cmd_dump` implementation with a version that uses `k_malloc` when the Kconfig is enabled:

```c
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

#if defined(CONFIG_TRACE_RECORDER_CMD_DUMP_MALLOC)
	/* Heap-allocated snapshot — reduces stack usage */
	struct trace_record *snapshot =
		(struct trace_record *)k_malloc(valid_count * sizeof(struct trace_record));

	if (snapshot != NULL) {
		memcpy(snapshot, trace_records, valid_count * sizeof(struct trace_record));
		k_spin_unlock(&trace_lock, key);

		shell_print(sh, "records_hex:");
		const uint8_t *raw = (const uint8_t *)snapshot;
		uint32_t total_bytes = valid_count * 8;
		for (uint32_t i = 0; i < total_bytes; i += 16) {
			uint32_t remain = total_bytes - i;
			if (remain >= 16) {
				shell_print(sh,
					    "%02x%02x%02x%02x%02x%02x%02x%02x"
					    "%02x%02x%02x%02x%02x%02x%02x%02x",
					    raw[i], raw[i + 1], raw[i + 2], raw[i + 3], raw[i + 4],
					    raw[i + 5], raw[i + 6], raw[i + 7], raw[i + 8], raw[i + 9],
					    raw[i + 10], raw[i + 11], raw[i + 12], raw[i + 13],
					    raw[i + 14], raw[i + 15]);
			} else {
				char buf[128];
				int off = 0;
				for (uint32_t j = 0; j < remain; j++) {
					off += snprintf(buf + off, sizeof(buf) - off, "%02x", raw[i + j]);
				}
				shell_print(sh, "%s", buf);
			}
		}

		k_free(snapshot);
	} else {
		/* k_malloc failed — fall back to per-record iteration */
		k_spin_unlock(&trace_lock, key);
		shell_warn(sh, "k_malloc failed, falling back to per-record dump");
		shell_print(sh, "records_hex:");
		for (uint32_t i = 0; i < valid_count; i++) {
			struct trace_record rec = trace_records[i];
			const uint8_t *b = (const uint8_t *)&rec;
			shell_print(sh, "%02x%02x%02x%02x%02x%02x%02x%02x",
				    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
		}
	}
#else
	/* Per-record iteration — no heap, minimal stack */
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
				    b0[0], b0[1], b0[2], b0[3], b0[4], b0[5], b0[6], b0[7],
				    b1[0], b1[1], b1[2], b1[3], b1[4], b1[5], b1[6], b1[7]);
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
#endif

	shell_print(sh, "--- TRACE DUMP END ---");
	return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add lib/trace_recorder/src/trace_recorder_shell.c
git commit -m "feat(trace_recorder): use k_malloc in cmd_dump when CMD_DUMP_MALLOC enabled"
```

---

### Task 3: Add test for malloc-based dump path

**Files:**
- Modify: `tests/trace_recorder/testcase.yaml`
- Modify: `tests/trace_recorder/prj.conf`

- [ ] **Step 1: Add a new test configuration for malloc-based dump**

Add to `tests/trace_recorder/testcase.yaml`:

```yaml
  weather_station.trace_recorder_malloc:
    platform_allow: native_sim/native/64
    harness: ztest
    tags: trace_recorder
    extra_args:
      - CONFIG_TRACING=y
      - CONFIG_TRACING_USER=y
      - CONFIG_TRACE_RECORDER=y
      - CONFIG_TRACE_RECORDER_SHELL=y
      - CONFIG_TRACE_RECORDER_CMD_DUMP_MALLOC=y
      - CONFIG_TRACE_RECORDER_MAX_THREADS=4
      - CONFIG_TRACE_RECORDER_BUFFER_SIZE=16
      - CONFIG_HEAP_MEM_POOL_SIZE=4096
```

- [ ] **Step 2: Commit**

```bash
git add tests/trace_recorder/
git commit -m "test(trace_recorder): add malloc-based dump test configuration"
```

---

### Task 4: Verify full test suite passes

- [ ] **Step 1: Run all tests**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister \
  -p native_sim/native/64 \
  -T tests/ \
  --inline-logs -v -N \
  --outdir twister-out-malloc-fix
```

Expected: all test configurations pass (including the new malloc-based one).

- [ ] **Step 2: Run pre-commit**

```bash
pre-commit run --all-files
```

- [ ] **Step 3: Commit any fixes**

If any issues were found, fix and commit them.
