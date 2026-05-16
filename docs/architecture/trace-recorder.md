# Trace Recorder

> Design rationale: [ADR-016](../adr/ADR-016-trace-recorder.md).

The trace recorder captures thread context-switch events, ISR entries/exits, and
idle events in a static ring buffer for post-mortem dump via Renode. It has no
public API — it self-wires via `SYS_INIT` and overrides Zephyr's `__weak`
tracing hooks.

---

## Architecture

```
┌───────────────────────────────────────────────────────────┐
│  Zephyr kernel scheduling                                  │
│                                                           │
│  Thread switch ──┐                                        │
│  ISR enter/exit ─┼──► ___weak_ tracing hooks ──┐          │
│  Thread create ──┘                             │          │
│  Idle ─────────────────────────────────────────┘          │
│                                                           │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  lib/trace_recorder/  (strong override)             │  │
│  │                                                     │  │
│  │  sys_trace_thread_switched_in_user()                │  │
│  │  sys_trace_thread_switched_out_user()               │  │
│  │  sys_trace_thread_create_user()                     │  │
│  │  sys_trace_isr_enter_user()                         │  │
│  │  sys_trace_isr_exit_user()                          │  │
│  │  sys_trace_idle_user()                              │  │
│  │                                                     │  │
│  │  trace_write_record()  ──►  trace_records[N]        │  │
│  │                             (ring buffer, BSS)      │  │
│  │                             thread_names[N]          │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                           │
│  Shell: trace status | dump | clear                       │
└───────────────────────────────────────────────────────────┘
```

The library overrides Zephyr's `__weak` `_user` tracing hooks defined in
`tracing_user.c`. At `SYS_INIT APPLICATION 1`, it assigns sequential IDs to
all pre-existing threads and enables recording. Each hook writes an 8-byte
record to a ring buffer protected by a spinlock.

---

## Binary trace format

```c
struct trace_record {
    uint32_t timestamp;   /* k_cycle_get_32() */
    uint8_t  event_type;  /* TRACE_EVENT_* */
    uint8_t  event_data;  /* priority (SWITCHED_IN), 0 otherwise */
    uint16_t thread_id;   /* assigned ID, or 0xFFFF for ISR/idle */
};
```

| Event type | Value | Meaning |
|-----------|-------|---------|
| `TRACE_EVENT_SWITCHED_IN` | 0 | Thread became the current thread |
| `TRACE_EVENT_SWITCHED_OUT` | 1 | Thread was preempted or yielded |
| `TRACE_EVENT_CREATE` | 2 | New thread created (at SYS_INIT or runtime) |
| `TRACE_EVENT_ISR_ENTER` | 3 | Interrupt handler started |
| `TRACE_EVENT_ISR_EXIT` | 4 | Interrupt handler ended |
| `TRACE_EVENT_IDLE_ENTER` | 5 | Idle thread became current |

Each record is exactly 8 bytes with no padding. Timestamp is a 32-bit cycle
counter (`k_cycle_get_32()`). Thread ID `0xFFFF` denotes unknown context
(ISR or idle).

---

## Ring buffer design

The buffer is a static BSS array — no heap allocation:

```c
struct trace_record trace_records[CONFIG_TRACE_RECORDER_BUFFER_SIZE];
```

Default 4096 records (32 KB). A spinlock protects writes from any context
(thread, ISR, idle). On wraparound, `trace_overflow` increments — this is
visible via `trace status`.

The buffer is designed for Renode `sysbus ReadMemory` post-mortem dump:
the symbol `trace_records` is visible in the ELF, and Renode reads the
entire buffer directly from the simulated address space.

---

## Thread identification

Each `k_thread` stores its assigned ID in `custom_data` (requires
`CONFIG_THREAD_CUSTOM_DATA=y`, auto-selected by the Kconfig). On context
switch in, the ID is read from `k_sched_current_thread_query()->custom_data`
— O(1) lookup with no pointer chasing.

A parallel static table maps ID → name:

```c
char trace_thread_names[CONFIG_TRACE_RECORDER_MAX_THREADS][CONFIG_THREAD_MAX_NAME_LEN];
```

Names are written at thread creation time and dumped alongside records
by the shell `trace dump` command and by Renode.

---

## SYS_INIT ordering

The trace recorder initializes at `APPLICATION 1` — the earliest application
priority, immediately after the kernel is ready. This ensures all subsequent
threads (sensors, HTTP, MQTT at priorities 80–99) are captured by the
`sys_trace_thread_create_user` hook.

Pre-existing threads (idle, main, shell, logging, workqueue) are enumerated
via `k_thread_foreach()` and assigned IDs during init.

---

## Shell commands

| Command | Purpose |
|---------|---------|
| `trace status` | Prints head position, buffer size, overflow count |
| `trace dump` | Hex dump of trace records + thread name table |
| `trace clear` | Resets head and overflow counter to zero |

Shell commands are gated by `CONFIG_TRACE_RECORDER_SHELL=y` (default on).

---

## Kconfig

| Symbol | Default | Purpose |
|--------|---------|---------|
| `CONFIG_TRACE_RECORDER` | n | Master enable (depends on `TRACING`) |
| `TRACE_RECORDER_BUFFER_SIZE` | 4096 | Number of 8-byte records (4–65536) |
| `TRACE_RECORDER_SHELL` | y | Shell subcommands (depends on `SHELL`) |
| `TRACE_RECORDER_CMD_DUMP_MALLOC` | n | Heap-based dump snapshot (test/debug only) |
| `TRACE_RECORDER_MAX_THREADS` | 16 | Thread ID namespace size (4–64) |

The library auto-selects `THREAD_CUSTOM_DATA` and depends on `TRACING`
(which implies `TRACING_USER`).

---

## Post-processing

The `scripts/parse-trace.py` script reads the binary trace dump and produces:

| Output | Format | Purpose |
|--------|--------|---------|
| Summary | stdout text | Thread runtime %, switch count, ISR count |
| Perfetto trace | Chrome Tracing JSON | Interactive timeline in Perfetto UI |
| Timeline JSON | JSON | Raw per-thread intervals for scripting |

See [renode-trace-workflow.md](renode-trace-workflow.md) for the full
development workflow.
