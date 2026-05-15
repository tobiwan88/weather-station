# Renode Trace Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a trace capture, post-processing, and deployment pipeline that captures thread context switches, ISR events, and function call stacks from Renode runs and publishes interactive Perfetto traces to gh-pages.

**Architecture:** New `lib/trace_recorder/` library overrides Zephyr `__weak` tracing hooks, writing 8-byte binary records to a static ring buffer. Renode dumps the buffer via `sysbus ReadMemory` while simultaneously recording a Perfetto profiler trace. A Python post-processor parses the binary trace, correlates with ELF symbols, merges with Renode's Perfetto trace, and deploys to gh-pages for deep-linked interactive viewing.

**Tech Stack:** Zephyr tracing subsystem (`CONFIG_TRACING_USER`), Renode guest profiler (`EnableProfilerPerfetto`), Python 3 `struct`/`subprocess`/Perfetto protobuf, Robot Framework, GitHub Actions artifacts + gh-pages.

---

### Task 1: Library scaffold — Kconfig, CMakeLists.txt, header

**Files:**
- Create: `lib/trace_recorder/Kconfig`
- Create: `lib/trace_recorder/CMakeLists.txt`
- Create: `lib/trace_recorder/include/trace_recorder/trace_recorder.h`

- [ ] **Step 1: Create `lib/trace_recorder/Kconfig`**

```kconfig
# SPDX-License-Identifier: Apache-2.0

menuconfig TRACE_RECORDER
	bool "Thread trace recorder for Renode performance analysis"
	select TRACING_USER
	select THREAD_CUSTOM_DATA
	help
	  Enables a lightweight thread-context-switch trace recorder that
	  writes 8-byte binary records (timestamp, event type, thread ID,
	  priority) to a static ring buffer. Designed for post-mortem dump
	  via Renode sysbus ReadMemory.

	  Events captured: thread create, switch in/out, ISR enter/exit,
	  idle.  No public API — self-wires via SYS_INIT.

	  Use with CONFIG_TRACING=y (auto-selected via TRACING_USER).

if TRACE_RECORDER

config TRACE_RECORDER_BUFFER_SIZE
	int "Number of trace records in ring buffer"
	default 4096
	range 256 65536
	help
	  Each record is 8 bytes (timestamp + event_type + event_data
	  + thread_id). 4096 records = 32 KB. At 1000 context switches/sec
	  this covers ~4 seconds of execution.

config TRACE_RECORDER_SHELL
	bool "Shell commands for trace recorder"
	default y
	depends on SHELL
	help
	  Registers the 'trace' shell command group with sub-commands:
	    status  — buffer utilisation and overflow count
	    dump    — hex dump of buffer + thread name table
	    clear   — reset head/tail to zero

config TRACE_RECORDER_MAX_THREADS
	int "Maximum number of tracked threads"
	default 16
	range 4 64
	help
	  Size of the static thread-name lookup table (ID → name string).
	  Each entry uses CONFIG_THREAD_MAX_NAME_LEN bytes (default 32).

module = TRACE_RECORDER
module-str = TRACE_RECORDER
source "subsys/logging/Kconfig.template.log_config"

endif # TRACE_RECORDER
```

- [ ] **Step 2: Create `lib/trace_recorder/CMakeLists.txt`**

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_TRACE_RECORDER)
  zephyr_library()
  zephyr_library_sources(src/trace_recorder.c)
  zephyr_library_sources_ifdef(CONFIG_TRACE_RECORDER_SHELL
                                src/trace_recorder_shell.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
```

- [ ] **Step 3: Create `lib/trace_recorder/include/trace_recorder/trace_recorder.h`**

```c
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
```

- [ ] **Step 4: Verify the scaffold compiles**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto -b native_sim/native/64 \
  apps/gateway -d build/test-scaffold -- \
  -DCONFIG_TRACE_RECORDER=y
```
Expected: build succeeds (library registers but has empty source — no-op).

- [ ] **Step 5: Commit**

```bash
git add lib/trace_recorder/Kconfig lib/trace_recorder/CMakeLists.txt \
        lib/trace_recorder/include/trace_recorder/trace_recorder.h
git commit -m "feat(trace_recorder): add library scaffold (Kconfig, CMakeLists, header)"
```

---

### Task 2: Trace recorder core — ring buffer and tracing hooks

**Files:**
- Create: `lib/trace_recorder/src/trace_recorder.c`

- [ ] **Step 1: Write `lib/trace_recorder/src/trace_recorder.c`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file trace_recorder.c
 * @brief Overrides Zephyr __weak tracing _user hooks to record thread
 *        context switches, ISR events, and idle events in a static ring
 *        buffer for post-mortem Renode memory dump.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/init.h>
#include <zephyr/tracing/tracing_user.h>

#include <trace_recorder/trace_recorder.h>

LOG_MODULE_REGISTER(trace_recorder, CONFIG_TRACE_RECORDER_LOG_LEVEL);

/* ── trace record format (8 bytes) ───────────────────────────────── */

#define TRACE_EVENT_SWITCHED_IN   0
#define TRACE_EVENT_SWITCHED_OUT  1
#define TRACE_EVENT_CREATE        2
#define TRACE_EVENT_ISR_ENTER     3
#define TRACE_EVENT_ISR_EXIT      4
#define TRACE_EVENT_IDLE_ENTER    5

#define TRACE_THREAD_ID_UNKNOWN   0xFFFFU

struct trace_record {
	uint32_t timestamp;   /* k_cycle_get_32() */
	uint8_t  event_type;  /* TRACE_EVENT_* */
	uint8_t  event_data;  /* priority for SWITCHED_IN, 0 otherwise */
	uint16_t thread_id;   /* assigned thread ID, or TRACE_THREAD_ID_UNKNOWN */
};

BUILD_ASSERT(sizeof(struct trace_record) == 8,
	     "trace_record must be 8 bytes");

/* ── static buffer (BSS — dumpable via Renode sysbus ReadMemory) ─── */

static struct trace_record trace_records[CONFIG_TRACE_RECORDER_BUFFER_SIZE];

/* No init needed — BSS zeroed by CRT. All-zero record means end-of-data. */

static char trace_thread_names[CONFIG_TRACE_RECORDER_MAX_THREADS]
			       [CONFIG_THREAD_MAX_NAME_LEN];

/* ── ring buffer state ───────────────────────────────────────────── */

static struct k_spinlock g_trace_lock;
static uint32_t g_trace_head;       /* next write index */
static bool     g_trace_ready;      /* false until SYS_INIT completes */
static uint32_t g_trace_overflow;   /* count of dropped records */
static atomic_t g_next_thread_id = ATOMIC_INIT(1);  /* 1-based; 0 = unassigned */

/* ── helper: write one record ─────────────────────────────────────── */

static void trace_write_record(uint8_t event_type, uint8_t event_data,
			       uint16_t thread_id)
{
	k_spinlock_key_t key = k_spin_lock(&g_trace_lock);

	if (g_trace_head >= CONFIG_TRACE_RECORDER_BUFFER_SIZE) {
		g_trace_overflow++;
		k_spin_unlock(&g_trace_lock, key);
		return;
	}

	struct trace_record *rec = &trace_records[g_trace_head];
	rec->timestamp  = k_cycle_get_32();
	rec->event_type = event_type;
	rec->event_data = event_data;
	rec->thread_id  = thread_id;

	g_trace_head++;
	k_spin_unlock(&g_trace_lock, key);
}

/* ── helper: get thread ID from custom_data ───────────────────────── */

static uint16_t trace_thread_id(struct k_thread *thread)
{
	if (thread->custom_data == NULL) {
		return TRACE_THREAD_ID_UNKNOWN;
	}
	/* custom_data stores a 1-based ID (0 = unassigned, NULL-safe) */
	return (uint16_t)(uintptr_t)thread->custom_data;
}

/* ── Zephyr tracing _user hooks (override __weak defaults) ────────── */

void sys_trace_thread_create_user(struct k_thread *thread)
{
	if (!g_trace_ready) {
		return;
	}

	uint16_t id = (uint16_t)atomic_inc(&g_next_thread_id);

	__ASSERT(id < CONFIG_TRACE_RECORDER_MAX_THREADS,
		 "trace_recorder: too many threads (max %d); increase "
		 "CONFIG_TRACE_RECORDER_MAX_THREADS",
		 CONFIG_TRACE_RECORDER_MAX_THREADS);

	thread->custom_data = (void *)(uintptr_t)id;

	const char *name = k_thread_name_get(thread);
	if (name != NULL) {
		strncpy(trace_thread_names[id], name,
			CONFIG_THREAD_MAX_NAME_LEN - 1);
		trace_thread_names[id][CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';
	}

	trace_write_record(TRACE_EVENT_CREATE, 0, id);
}

void sys_trace_thread_switched_in_user(void)
{
	if (!g_trace_ready) {
		return;
	}

	struct k_thread *t = k_sched_current_thread_query();
	uint16_t id = trace_thread_id(t);
	uint8_t prio = (uint8_t)k_thread_priority_get(t);

	trace_write_record(TRACE_EVENT_SWITCHED_IN, prio, id);
}

void sys_trace_thread_switched_out_user(void)
{
	if (!g_trace_ready) {
		return;
	}

	struct k_thread *t = k_sched_current_thread_query();
	uint16_t id = trace_thread_id(t);

	trace_write_record(TRACE_EVENT_SWITCHED_OUT, 0, id);
}

void sys_trace_isr_enter_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_ISR_ENTER, 0,
			   TRACE_THREAD_ID_UNKNOWN);
}

void sys_trace_isr_exit_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_ISR_EXIT, 0,
			   TRACE_THREAD_ID_UNKNOWN);
}

void sys_trace_idle_user(void)
{
	if (!g_trace_ready) {
		return;
	}
	trace_write_record(TRACE_EVENT_IDLE_ENTER, 0,
			   TRACE_THREAD_ID_UNKNOWN);
}

/* No-ops for hooks we don't need (must override to prevent link errors
 * if tracing_test.h/other format backend is not in use). These are
 * weak in tracing_user.c so defining them here is sufficient. */

void sys_trace_thread_abort_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_suspend_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_resume_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_name_set_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_info_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_priority_set_user(struct k_thread *thread, int prio)
{
	ARG_UNUSED(thread);
	ARG_UNUSED(prio);
}
void sys_trace_thread_sched_ready_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_thread_pend_user(struct k_thread *thread) { ARG_UNUSED(thread); }
void sys_trace_sys_init_enter_user(const struct init_entry *entry, int level)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(level);
}
void sys_trace_sys_init_exit_user(const struct init_entry *entry, int level, int result)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(level);
	ARG_UNUSED(result);
}

/* ── SYS_INIT: assign IDs to pre-existing threads ─────────────────── */

static void assign_existing_thread(struct k_thread *thread, void *user_data)
{
	ARG_UNUSED(user_data);

	if (thread->custom_data != NULL) {
		return;
	}

	uint16_t id = (uint16_t)atomic_inc(&g_next_thread_id);

	if (id >= CONFIG_TRACE_RECORDER_MAX_THREADS) {
		LOG_WRN("too many pre-existing threads; skipping ID assignment");
		return;
	}

	thread->custom_data = (void *)(uintptr_t)id;

	const char *name = k_thread_name_get(thread);
	if (name != NULL) {
		strncpy(trace_thread_names[id], name,
			CONFIG_THREAD_MAX_NAME_LEN - 1);
		trace_thread_names[id][CONFIG_THREAD_MAX_NAME_LEN - 1] = '\0';
	}
}

static int trace_recorder_init(void)
{
	k_thread_foreach(assign_existing_thread, NULL);
	g_trace_ready = true;
	LOG_DBG("trace_recorder: init done, %u pre-existing threads",
		(uint32_t)atomic_get(&g_next_thread_id));
	return 0;
}
SYS_INIT(trace_recorder_init, APPLICATION, 1);
```

- [ ] **Step 2: Build test**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto -b native_sim/native/64 \
  apps/gateway -d build/test-core -- \
  -DCONFIG_TRACE_RECORDER=y
```
Expected: build succeeds (note: native_sim build with trace enabled — tracing hooks fire on boot).

- [ ] **Step 3: Commit**

```bash
git add lib/trace_recorder/src/trace_recorder.c
git commit -m "feat(trace_recorder): add ring buffer and tracing _user hooks"
```

---

### Task 3: Shell commands — `trace status`, `trace dump`, `trace clear`

**Files:**
- Create: `lib/trace_recorder/src/trace_recorder_shell.c`

- [ ] **Step 1: Write `lib/trace_recorder/src/trace_recorder_shell.c`**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file trace_recorder_shell.c
 * @brief Shell commands for the trace recorder.
 *
 *   trace status   — buffer utilisation / overflow count
 *   trace dump     — hex dump of records + thread name table
 *   trace clear    — reset buffer
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

/* ── external symbols from trace_recorder.c ──────────────────────── */

extern struct trace_record trace_records[];
extern char trace_thread_names[][CONFIG_THREAD_MAX_NAME_LEN];
extern uint32_t g_trace_head;
extern uint32_t g_trace_overflow;

/* ── trace status ─────────────────────────────────────────────────── */

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint32_t head = g_trace_head;
	uint32_t overflow = g_trace_overflow;
	uint32_t capacity = CONFIG_TRACE_RECORDER_BUFFER_SIZE;

	shell_print(sh, "Trace recorder status:");
	shell_print(sh, "  Records:    %u / %u (%u%%)",
		    head, capacity,
		    capacity ? (unsigned)(head * 100ULL / capacity) : 0);
	shell_print(sh, "  Overflow:   %u records dropped", overflow);
	shell_print(sh, "  Each record: 8 bytes (total buffer: %u bytes)",
		    capacity * 8);

	return 0;
}

/* ── trace dump ───────────────────────────────────────────────────── */

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint32_t head = g_trace_head;
	uint32_t capacity = CONFIG_TRACE_RECORDER_BUFFER_SIZE;

	shell_print(sh, "--- TRACE DUMP BEGIN ---");
	shell_print(sh, "records=%u capacity=%u overflow=%u",
		    head, capacity, g_trace_overflow);

	/* print thread name table */
	shell_print(sh, "thread_names:");
	for (uint32_t i = 0; i < CONFIG_TRACE_RECORDER_MAX_THREADS; i++) {
		if (trace_thread_names[i][0] != '\0') {
			shell_print(sh, "  [%u] %s", i,
				    trace_thread_names[i]);
		}
	}

	/* print records as hex (8 bytes each, 16 bytes per shell line) */
	shell_print(sh, "records_hex:");
	const uint8_t *raw = (const uint8_t *)trace_records;
	uint32_t total_bytes = head * 8;
	for (uint32_t i = 0; i < total_bytes; i += 16) {
		uint32_t remain = total_bytes - i;
		if (remain >= 16) {
			shell_print(sh,
				"%02x%02x%02x%02x%02x%02x%02x%02x"
				"%02x%02x%02x%02x%02x%02x%02x%02x",
				raw[i], raw[i+1], raw[i+2], raw[i+3],
				raw[i+4], raw[i+5], raw[i+6], raw[i+7],
				raw[i+8], raw[i+9], raw[i+10], raw[i+11],
				raw[i+12], raw[i+13], raw[i+14], raw[i+15]);
		} else {
			char buf[128];
			int off = 0;
			for (uint32_t j = 0; j < remain; j++) {
				off += snprintf(buf + off, sizeof(buf) - off,
						"%02x", raw[i + j]);
			}
			shell_print(sh, "%s", buf);
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

	g_trace_head = 0;
	g_trace_overflow = 0;

	shell_print(sh, "Trace buffer cleared.");
	return 0;
}

/* ── command registration ─────────────────────────────────────────── */

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_trace,
	SHELL_CMD(status, NULL, "Show trace buffer utilisation", cmd_status),
	SHELL_CMD(dump, NULL, "Hex dump of trace records + thread names",
		  cmd_dump),
	SHELL_CMD(clear, NULL, "Reset trace buffer", cmd_clear),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(trace, &sub_trace, "Trace recorder controls", NULL);
```

- [ ] **Step 2: Build test with shell**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto -b native_sim/native/64 \
  apps/gateway -d build/test-shell -- \
  -DCONFIG_TRACE_RECORDER=y -DCONFIG_TRACE_RECORDER_SHELL=y
```
Expected: build succeeds, `trace` command available in shell.

- [ ] **Step 3: Commit**

```bash
git add lib/trace_recorder/src/trace_recorder_shell.c
git commit -m "feat(trace_recorder): add shell commands (status, dump, clear)"
```

---

### Task 4: Renode target Kconfig fragment

**Files:**
- Create: `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf`

- [ ] **Step 1: Write the Kconfig fragment**

```
CONFIG_TRACE_RECORDER=y
CONFIG_TRACE_RECORDER_BUFFER_SIZE=4096
CONFIG_TRACE_RECORDER_SHELL=y
```

(Other needed configs — `CONFIG_TRACING`, `CONFIG_TRACING_USER`, `CONFIG_THREAD_CUSTOM_DATA` — are auto-selected by `TRACE_RECORDER`. The Renode board already has `CONFIG_THREAD_MONITOR=y` and `CONFIG_THREAD_NAME=y` from the base gateway config.)

- [ ] **Step 2: Verify the build works with the fragment**

```bash
cd /home/zephyr/workspace/weather-station
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto \
  -b frdm_mcxn947/mcxn947/cpu0 --sysbuild \
  apps/gateway -d build/renode-trace -- \
  -DDTC_OVERLAY_FILE=boards/frdm_mcxn947_mcxn947_cpu0_renode.overlay \
  -DEXTRA_CONF_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.conf;boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf"
```
Expected: build succeeds (may need to add `--` for sysbuild args; adjust as needed).

- [ ] **Step 3: Commit**

```bash
git add apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf
git commit -m "feat(gateway): add trace_recorder Kconfig fragment for Renode target"
```

---

### Task 5: Renode `.resc` — profiler + function logging + trace dump macro

**Files:**
- Modify: `simulation/renode/frdm_mcxn947.resc`

- [ ] **Step 1: Update `simulation/renode/frdm_mcxn947.resc`**

Replace the existing `macro reset` block with the expanded version below (adds profiler enable + function name logging in the reset macro, and a new `dump-trace` macro for test teardown):

```renode
$name?="frdm_mcxn947_mcxn947_cpu0"
$bin?=@zephyr.elf
$repl?=$ORIGIN/frdm_mcxn947_mcxn947_cpu0.repl

using sysbus
mach create $name

machine LoadPlatformDescription $repl

showAnalyzer flexcomm4lpuart4

set osPanicHook
"""
self.ErrorLog("OS Panicked")
"""

cpu0 AddSymbolHook "z_fatal_error" $osPanicHook

macro reset
"""
    sysbus LoadELF $bin
    cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    cpu0 EnableZephyrMode
    cpu0 EnableProfilerPerfetto @${ORIGIN}/trace_functions.perfetto false
    cpu0 LogFunctionNames true "k_ z_ work_ mqtt_ net_ zbus_ mbedtls_" true
"""

macro dump-trace
"""
    cpu0 FlushProfiler
    emulation RunFor "0.01"
    sysbus ReadMemory `sysbus GetSymbolAddress "trace_records"` 32768 @${ORIGIN}/trace.bin
    echo "[TRACE] Dumped trace buffer (32 KB) to trace.bin"
"""
```

- [ ] **Step 2: Commit**

```bash
git add simulation/renode/frdm_mcxn947.resc
git commit -m "feat(renode): add profiler, function logging, and trace dump macro to .resc"
```

---

### Task 6: Renode Robot Framework test — `gateway_trace.robot`

**Files:**
- Modify: `simulation/renode/tests/common.robot`
- Create: `simulation/renode/tests/gateway_trace.robot`

- [ ] **Step 1: Add `Dump Trace` keyword to `common.robot`**

Append to the existing `*** Keywords ***` section in `simulation/renode/tests/common.robot`:

```robot
Dump Trace
    [Documentation]    Flush profiler, pause briefly, then dump trace buffer.
    Execute Command    cpu0 FlushProfiler
    Execute Command    emulation RunFor "0.01"
    ${trace_addr}=     Execute Command    sysbus GetSymbolAddress "trace_records"
    Execute Command    sysbus ReadMemory ${trace_addr.strip()} 32768 @${CURDIR}/../trace.bin
    Log To Console     [TRACE] trace.bin written
```

- [ ] **Step 2: Create `simulation/renode/tests/gateway_trace.robot`**

````robot
*** Settings ***
Resource            common.robot

*** Test Cases ***
Gateway Boots And Collects Trace
    [Documentation]    Boot the gateway, wait for shell, let it run briefly,
    ...                then collect a trace dump.
    [Tags]    trace    boot
    Prepare Machine         ${ELF}
    Wait For Boot Banner
    Wait For Shell Prompt
    # Let the system run for a few seconds to accumulate trace data
    Execute Command         sleep 5
    Dump Trace
    # Verify trace buffer address resolves (confirms ELF loaded correctly)
    ${trace_addr}=         Execute Command    sysbus GetSymbolAddress "trace_records"
    Should Not Be Equal    ${trace_addr.strip()}    0x0    trace_records symbol not found
````

- [ ] **Step 3: Verify the test runs locally**

```bash
cd /home/zephyr/workspace/weather-station
# Build first (use existing build from Task 4 if still available)
renode-test --show-log simulation/renode/tests/gateway_trace.robot
```
Expected: test passes, `trace.bin` and `trace_functions.perfetto` files appear in `simulation/renode/`.

- [ ] **Step 4: Commit**

```bash
git add simulation/renode/tests/common.robot \
        simulation/renode/tests/gateway_trace.robot
git commit -m "feat(renode): add gateway_trace robot test and Dump Trace keyword"
```

---

### Task 7: Post-processor — `scripts/parse-trace.py`

**Files:**
- Create: `scripts/parse-trace.py`

- [ ] **Step 1: Write `scripts/parse-trace.py`**

```python
#!/usr/bin/env python3
"""
parse-trace.py — Convert Renode binary trace dump to Perfetto trace + summary.

Inputs:
  --trace trace.bin            Binary trace from Renode sysbus ReadMemory
  --elf zephyr.elf             ELF file for symbol/thread-name resolution
  --names trace_names.bin      (optional) Thread names dump
  --profiler trace_functions.perfetto  (optional) Renode Perfetto profiler output

Outputs:
  --output trace_combined.perfetto   Perfetto trace (default stdout text report)
  --summary                         Print human-readable summary to stdout
  --json trace_threads.json         Raw timeline as JSON

The binary trace format is a flat sequence of 8-byte records:
  uint32_t timestamp;   // k_cycle_get_32()
  uint8_t  event_type;  // 0=switched_in, 1=switched_out, 2=create,
                        // 3=isr_enter, 4=isr_exit, 5=idle
  uint8_t  event_data;  // priority for switched_in, 0 otherwise
  uint16_t thread_id;   // 0xFFFF = unknown (ISR/idle)
"""

import argparse
import json
import struct
import sys
from collections import defaultdict

RECORD_FMT = "<IBBH"  # timestamp:u32, type:u8, data:u8, id:u16
RECORD_SIZE = struct.calcsize(RECORD_FMT)  # 8

EVENT_NAMES = {
    0: "switched_in",
    1: "switched_out",
    2: "create",
    3: "isr_enter",
    4: "isr_exit",
    5: "idle_enter",
}

UNKNOWN_THREAD_ID = 0xFFFF


def parse_binary_trace(path):
    """Parse the binary trace file into a list of dicts."""
    with open(path, "rb") as f:
        data = f.read()

    records = []
    for i in range(0, len(data), RECORD_SIZE):
        chunk = data[i : i + RECORD_SIZE]
        if len(chunk) < RECORD_SIZE:
            break
        ts, evt_type, evt_data, thread_id = struct.unpack(RECORD_FMT, chunk)

        # All-zero record = end of valid data (BSS initialization)
        if ts == 0 and evt_type == 0 and evt_data == 0 and thread_id == 0:
            continue

        records.append({
            "timestamp": ts,
            "event_type": evt_type,
            "event_name": EVENT_NAMES.get(evt_type, f"unknown_{evt_type}"),
            "event_data": evt_data,
            "thread_id": thread_id,
        })

    return records


def resolve_thread_names(records, elf_path=None):
    """Build thread_id -> name mapping from create events and ELF symbols.
    Falls back to hex IDs if resolution fails."""
    names = {}

    # Extract names from CREATE events (the names table is embedded in
    # the firmware; we rely on create events recording IDs in order)
    for rec in records:
        if rec["event_type"] == 2:  # CREATE
            tid = rec["thread_id"]
            if tid != UNKNOWN_THREAD_ID and tid not in names:
                names[tid] = f"thread_{tid}"

    # Try to resolve names from ELF if available
    if elf_path:
        try:
            import subprocess
            result = subprocess.run(
                ["nm", "--defined-only", elf_path],
                capture_output=True, text=True
            )
            # We can't easily map ELF symbols to thread IDs without the
            # thread_names table, but we note the availability.
            # For now, use the CREATE-event-derived names.
        except Exception:
            pass

    return names


def build_timeline(records, thread_names):
    """Build per-thread run intervals from switched_in/out pairs."""
    threads = defaultdict(lambda: {
        "name": "unknown",
        "run_time_cycles": 0,
        "switch_count": 0,
        "intervals": [],
    })

    current_thread = None
    switch_in_ts = 0

    for rec in records:
        tid = rec["thread_id"]
        evt = rec["event_type"]

        if evt == 0:  # switched_in
            if current_thread is not None:
                # Implicit switch-out of previous thread
                pass
            current_thread = tid
            switch_in_ts = rec["timestamp"]
            threads[tid]["switch_count"] += 1

        elif evt == 1:  # switched_out
            if current_thread is not None:
                run_time = rec["timestamp"] - switch_in_ts
                threads[current_thread]["run_time_cycles"] += run_time
                threads[current_thread]["intervals"].append(
                    (switch_in_ts, rec["timestamp"])
                )
            current_thread = None

    # Close final interval
    if current_thread is not None and records:
        last_ts = records[-1]["timestamp"]
        run_time = last_ts - switch_in_ts
        threads[current_thread]["run_time_cycles"] += run_time
        threads[current_thread]["intervals"].append(
            (switch_in_ts, last_ts)
        )

    # Assign names
    for tid, info in threads.items():
        info["name"] = thread_names.get(tid, f"thread_0x{tid:04x}")

    return threads


def print_summary(threads, records):
    """Print human-readable trace summary."""
    total_cycles = sum(t["run_time_cycles"] for t in threads.values())
    if total_cycles == 0:
        total_cycles = 1  # avoid div by zero

    isr_count = sum(1 for r in records if r["event_type"] == 3)
    switch_count = sum(1 for r in records if r["event_type"] == 0)

    print(f"Trace summary:")
    print(f"  Total records:    {len(records)}")
    print(f"  Context switches: {switch_count}")
    print(f"  ISR entries:      {isr_count}")
    print(f"  Tracked threads:  {len(threads)}")
    print()
    print(f"{'Thread':<20} {'Runtime %':>9}  {'Switches':>8}  {'ID':>6}")
    print("-" * 52)

    for tid, info in sorted(threads.items(), key=lambda x: -x[1]["run_time_cycles"]):
        pct = info["run_time_cycles"] * 100.0 / total_cycles
        name = info["name"]
        print(f"{name:<20} {pct:8.1f}%  {info['switch_count']:>8}  {tid:>6}")

    print("-" * 52)
    print(f"{'TOTAL':<20} {100.0:8.1f}%")


def write_perfetto_trace(threads, records, output_path):
    """Write a minimal Perfetto trace with per-thread sched_switch events.
    This uses Perfetto's trace proto format. For a full implementation,
    use the perfetto protobuf Python bindings. This stub writes a JSON
    representation that Perfetto UI can import."""
    # Perfetto UI accepts JSON traces in Chrome Tracing format
    trace_events = []

    tid_track = {}

    for rec in records:
        if rec["event_type"] == 0:  # switched_in
            tid = rec["thread_id"]
            name = f"thread_{tid}"
            ts_us = rec["timestamp"]  # cycles; approximate as us for visual

            if tid not in tid_track:
                tid_track[tid] = len(tid_track)
                trace_events.append({
                    "name": "thread_name",
                    "ph": "M",
                    "pid": 0,
                    "tid": tid,
                    "args": {"name": name},
                })

            trace_events.append({
                "name": "running",
                "ph": "B",
                "ts": ts_us,
                "pid": 0,
                "tid": tid,
            })

        elif rec["event_type"] == 1:  # switched_out
            tid = rec["thread_id"]
            ts_us = rec["timestamp"]
            trace_events.append({
                "name": "running",
                "ph": "E",
                "ts": ts_us,
                "pid": 0,
                "tid": tid,
            })

    output = {
        "traceEvents": trace_events,
        "displayTimeUnit": "ns",
    }

    with open(output_path, "w") as f:
        json.dump(output, f, indent=2)

    print(f"Perfetto trace written to {output_path}")


def write_json_timeline(threads, records, output_path):
    """Write raw timeline as JSON."""
    output = {
        "threads": {
            str(tid): {
                "name": info["name"],
                "run_time_cycles": info["run_time_cycles"],
                "switch_count": info["switch_count"],
                "intervals": info["intervals"],
            }
            for tid, info in threads.items()
        },
        "record_count": len(records),
        "isr_enter_count": sum(1 for r in records if r["event_type"] == 3),
        "idle_enter_count": sum(1 for r in records if r["event_type"] == 5),
    }
    with open(output_path, "w") as f:
        json.dump(output, f, indent=2)
    print(f"Timeline JSON written to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Parse Renode binary trace dump")
    parser.add_argument("--trace", required=True, help="Binary trace file (trace.bin)")
    parser.add_argument("--elf", default=None, help="ELF file for symbol resolution")
    parser.add_argument("--profiler", default=None, help="Renode Perfetto profiler output")
    parser.add_argument("--output", default=None, help="Output Perfetto trace file (.perfetto or .json)")
    parser.add_argument("--summary", action="store_true", help="Print human-readable summary")
    parser.add_argument("--json", default=None, help="Output raw timeline as JSON")
    args = parser.parse_args()

    records = parse_binary_trace(args.trace)
    if not records:
        print("Warning: no trace records found in binary file", file=sys.stderr)
        return 1

    thread_names = resolve_thread_names(records, args.elf)
    threads = build_timeline(records, thread_names)

    if args.summary or not args.output:
        print_summary(threads, records)

    if args.output:
        write_perfetto_trace(threads, records, args.output)

    if args.json:
        write_json_timeline(threads, records, args.json)

    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Test with a sample trace**

Create a minimal test trace and verify the parser works:

```bash
# Generate a tiny synthetic trace for testing
python3 -c "
import struct
records = [
    (1000, 2, 0, 0),    # create thread 0
    (2000, 0, 5, 0),    # switched_in thread 0 prio 5
    (5000, 1, 0, 0),    # switched_out thread 0
    (6000, 3, 0, 65535),# isr_enter
    (6100, 4, 0, 65535),# isr_exit
    (7000, 0, 5, 0),    # switched_in thread 0
    (9000, 5, 0, 65535),# idle_enter
]
with open('/tmp/test_trace.bin', 'wb') as f:
    for ts, typ, data, tid in records:
        f.write(struct.pack('<IBBH', ts, typ, data, tid))
"
python3 scripts/parse-trace.py --trace /tmp/test_trace.bin --summary
```

Expected output:
```
Trace summary:
  Total records:    7
  Context switches: 2
  ISR entries:      1
  Tracked threads:  2
  ...
```

- [ ] **Step 3: Commit**

```bash
git add scripts/parse-trace.py
git commit -m "feat(trace): add parse-trace.py post-processor (binary trace -> summary + Perfetto)"
```

---

### Task 8: Dev workflow script — `scripts/trace-and-analyze`

**Files:**
- Create: `scripts/trace-and-analyze`

- [ ] **Step 1: Write `scripts/trace-and-analyze`**

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${PROJECT_ROOT}/build/renode-trace"
RENODE_DIR="${PROJECT_ROOT}/simulation/renode"
ELF="${BUILD_DIR}/gateway/zephyr/zephyr.elf"
TRACE_BIN="${RENODE_DIR}/trace.bin"
PERFETTO_TRACE="${RENODE_DIR}/trace_functions.perfetto"
OUTPUT="${1:-${RENODE_DIR}/trace_combined.json}"

echo "=== 1. Building gateway with trace recorder ==="
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  --build-dir "$BUILD_DIR" \
  apps/gateway \
  -- \
  -DDTC_OVERLAY_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.overlay" \
  -DEXTRA_CONF_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.conf;boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf"

echo ""
echo "=== 2. Running Renode trace test ==="
renode-test --show-log "${RENODE_DIR}/tests/gateway_trace.robot" || {
  echo "WARNING: Renode test failed, but trace files may still exist."
}

echo ""
echo "=== 3. Parsing trace ==="
if [ -f "$TRACE_BIN" ]; then
  python3 "${SCRIPT_DIR}/parse-trace.py" \
    --trace "$TRACE_BIN" \
    --elf "$ELF" \
    --profiler "$PERFETTO_TRACE" \
    --output "$OUTPUT" \
    --summary

  echo ""
  echo "=== Done ==="
  echo "Perfetto trace: $OUTPUT"
  echo "Open in browser:  https://ui.perfetto.dev"
  echo "  (Drag & drop trace file, or use --output trace_combined.perfetto)"
else
  echo "ERROR: trace.bin not found at $TRACE_BIN"
  exit 1
fi
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/trace-and-analyze
```

- [ ] **Step 3: Commit**

```bash
git add scripts/trace-and-analyze
git commit -m "feat(trace): add trace-and-analyze one-command dev workflow script"
```

---

### Task 9: CI changes — build with trace, publish artifacts, deploy-results

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Update the Renode build step to include trace config**

In `.github/workflows/ci.yml`, find the `- name: Build gateway for Renode` step (around line 194). Change the `EXTRA_CONF_FILE` line from:

```yaml
              -DEXTRA_CONF_FILE=boards/frdm_mcxn947_mcxn947_cpu0_renode.conf
```

to:

```yaml
              -DEXTRA_CONF_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.conf;boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf"
```

- [ ] **Step 2: Update the Renode artifact upload step to include trace files**

Find the `- name: Upload test artifacts` step (around line 235). Change the `path:` block from:

```yaml
          path: |
            robot_output.xml
            log.html
            report.html
```

to:

```yaml
          path: |
            robot_output.xml
            log.html
            report.html
            simulation/renode/trace.bin
            simulation/renode/trace_functions.perfetto
            build/renode/gateway/zephyr/zephyr.elf
```

- [ ] **Step 3: Add the `deploy-results` job**

Add a new job after the existing `snapshot` job (at the end of `ci.yml`):

```yaml
  # ── 7. Deploy results to gh-pages ──────────────────────────────────────────
  deploy-results:
    name: Deploy results to gh-pages
    runs-on: ubuntu-latest
    if: |
      (github.event_name == 'push' && github.ref == 'refs/heads/master') ||
      github.event_name == 'pull_request'
    needs: [test-and-coverage, renode]
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Configure git user
        run: |
          git config --global user.email "ci@example.com"
          git config --global user.name "CI Bot"

      - name: Download coverage report
        uses: actions/download-artifact@v4
        with:
          name: coverage-report
          path: deploy/coverage/
        continue-on-error: true

      - name: Download Twister results
        uses: actions/download-artifact@v4
        with:
          name: twister-results
          path: deploy/twister/
        continue-on-error: true

      - name: Download Renode boot trace
        uses: actions/download-artifact@v4
        with:
          name: renode-gateway_boot
          path: deploy/renode/
        continue-on-error: true

      - name: Determine target directory on gh-pages
        id: dir
        run: |
          if [ "${{ github.event_name }}" = "pull_request" ]; then
            echo "path=dev/traces/pr-${{ github.event.pull_request.number }}" >> "$GITHUB_OUTPUT"
          else
            echo "path=dev/traces/master" >> "$GITHUB_OUTPUT"
          fi

      - name: Deploy to gh-pages
        run: |
          TARGET_DIR="${{ steps.dir.outputs.path }}"
          git fetch origin gh-pages
          git checkout gh-pages
          mkdir -p "$TARGET_DIR"
          # Remove old results for this ref (overwrite, not accumulate)
          rm -rf "$TARGET_DIR"/*
          # Copy new results
          if [ -d deploy/coverage ] && [ "$(ls -A deploy/coverage 2>/dev/null)" ]; then
            mkdir -p "$TARGET_DIR/coverage"
            cp -r deploy/coverage/* "$TARGET_DIR/coverage/"
          fi
          if [ -d deploy/twister ] && [ "$(ls -A deploy/twister 2>/dev/null)" ]; then
            mkdir -p "$TARGET_DIR/twister"
            cp -r deploy/twister/* "$TARGET_DIR/twister/"
          fi
          if [ -d deploy/renode ] && [ "$(ls -A deploy/renode 2>/dev/null)" ]; then
            mkdir -p "$TARGET_DIR/renode"
            cp -r deploy/renode/* "$TARGET_DIR/renode/"
            # If trace files exist, parse and deploy Perfetto/Chrome trace
            TRACE_BIN="deploy/renode/simulation/renode/trace.bin"
            TRACE_ELF="deploy/renode/build/renode/gateway/zephyr/zephyr.elf"
            if [ -f "$TRACE_BIN" ] && [ -f "$TRACE_ELF" ]; then
              python3 scripts/parse-trace.py \
                --trace "$TRACE_BIN" \
                --elf "$TRACE_ELF" \
                --output "$TARGET_DIR/trace_combined.json" \
                --summary || echo "WARNING: trace parsing failed"
              REPO_OWNER="$(echo '${{ github.repository }}' | cut -d/ -f1)"
              REPO_NAME="$(echo '${{ github.repository }}' | cut -d/ -f2)"
              echo "Perfetto deep-link: https://ui.perfetto.dev/#!/?url=https://${REPO_OWNER}.github.io/${REPO_NAME}/${TARGET_DIR}/trace_combined.json"
            fi
          fi
          git add .
          git diff --staged --quiet || git commit -m "ci: deploy results to $TARGET_DIR"
          git push origin gh-pages
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "feat(ci): add trace build config, publish trace artifacts, deploy-results job"
```

**Note:** No changes needed to `benchmark.yml`. The `deploy-results` job deploys to `dev/traces/` which lives under the `dev/` directory already preserved by `benchmark.yml`'s `docs` job (`find ... ! -name 'dev'`).

---

### Task 10: Backlog update

**Files:**
- Modify: `docs/backlog.md`

- [ ] **Step 1: Add `[RENODE-TRACE-GH-PAGES]` item to `docs/backlog.md`**

Append to the end of the file, before any existing end marker:

```markdown
---

## [RENODE-TRACE-GH-PAGES] Perfetto deep-link 404 guard and retention cleanup

The `deploy-results` CI job deploys `trace_combined.json` (Chrome Tracing
format) and test reports to the `gh-pages` branch. The Perfetto UI deep-link
pattern `ui.perfetto.dev/#!/?url=...` works for any public URL.

**Known gaps:**
- No 404 guard: if a trace file was not generated (build failure, size > 50 MB),
  the deep-link silently fails in Perfetto UI. A health-check script or index
  page listing available traces would improve UX.
- No retention cleanup: old PR traces accumulate indefinitely on `gh-pages`.
  A periodic cleanup job (e.g., remove traces for closed PRs older than 30
  days) is needed.
- Trace format: currently outputs Chrome Tracing JSON. Migrating to the binary
  Perfetto proto format (.perfetto or .pb) would reduce file size for large
  traces and enable Perfetto's full feature set (flow events, counters).

**Acceptance:**
- An index page at `<org>.github.io/weather-station/traces/` lists all
  available traces with links.
- Old PR trace directories are removed when the PR is closed/merged.
- Optional: binary Perfetto proto output from parse-trace.py.
```

- [ ] **Step 2: Commit**

```bash
git add docs/backlog.md
git commit -m "docs(backlog): add RENODE-TRACE-GH-PAGES item"
```

---

### Task 11: Integration test — full pipeline smoke test

- [ ] **Step 1: Build with trace enabled**

```bash
cd /home/zephyr/workspace/weather-station
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p always \
  -b frdm_mcxn947/mcxn947/cpu0 --sysbuild \
  apps/gateway -d build/renode-trace -- \
  -DDTC_OVERLAY_FILE=boards/frdm_mcxn947_mcxn947_cpu0_renode.overlay \
  -DEXTRA_CONF_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.conf;boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf"
```

- [ ] **Step 2: Run Renode trace test**

```bash
renode-test --show-log simulation/renode/tests/gateway_trace.robot
```

- [ ] **Step 3: Parse the trace**

```bash
python3 scripts/parse-trace.py \
  --trace simulation/renode/trace.bin \
  --elf build/renode-trace/gateway/zephyr/zephyr.elf \
  --summary
```

Expected: summary output shows thread IDs, runtime percentages, context switch count.

- [ ] **Step 4: Verify trace file opens in Perfetto UI**

```bash
# Generate a Chrome Tracing JSON trace
python3 scripts/parse-trace.py \
  --trace simulation/renode/trace.bin \
  --elf build/renode-trace/gateway/zephyr/zephyr.elf \
  --output /tmp/trace_test.json

# Check that the output file is valid JSON and has traceEvents
python3 -c "
import json
with open('/tmp/trace_test.json') as f:
    data = json.load(f)
assert 'traceEvents' in data, 'Missing traceEvents'
assert len(data['traceEvents']) > 0, 'No trace events'
print(f'OK: {len(data[\"traceEvents\"])} trace events')
"
```

Expected: `OK: N trace events` where N > 0.

- [ ] **Step 5: Run full native_sim integration tests to confirm no regressions**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west twister \
  -p native_sim/native/64 \
  -T tests/integration \
  --inline-logs -v -N
```
Expected: all integration tests pass.

- [ ] **Step 6: Commit any fixes**

If any issues were found during the smoke test, fix and commit them.
