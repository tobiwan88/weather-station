# Renode Trace Analysis — Design Spec

> **Date:** 2026-05-15
> **Status:** Approved
> **Constrained by:** ADR-008, ADR-009, ADR-012

## Goal

Add a trace capture and analysis pipeline for the Renode FRDM-MCXN947 target, enabling thread-level performance optimization via continuous thread-context-switch tracing, function-level call stacks (Perfetto), and peripheral access logging — all captured from a single Renode run and viewable in Perfetto UI.

## Context

The FRDM-MCXN947 gateway runs a multi-threaded Zephyr application (sntp_sync, mqtt_publisher, remote_sensor manager, http_dashboard server, system workqueue, etc.). We need visibility into which threads run when, for how long, and what they spend CPU time on — to identify thread starvation, priority inversion, ISR latency, and scheduling inefficiencies before they ship.

The project already has Renode integration (`simulation/renode/`) with Robot Framework tests for boot and FOTA. No tracing or profiling is currently enabled. The Zephyr tracing subsystem (`CONFIG_TRACING`) provides thread-switch, ISR, idle, and kernel-object hooks but is unused. Renode offers guest profiling (Perfetto flame graphs), function name logging, and peripheral access logging natively.

## Decisions

### Trace capture: Zephyr TRACING_USER + ring buffer + Renode memory dump

Use `CONFIG_TRACING=y` + `CONFIG_TRACING_USER=y` with a new `lib/trace_recorder/` library that overrides Zephyr's `__weak` tracing hooks. Thread switch, ISR enter/exit, idle, and thread create events are written as compact 8-byte binary records to a static ring buffer (BSS). After emulation stops, Renode dumps the buffer via `sysbus ReadMemory`.

**Alternatives considered:**
- **UART streaming backend** — continuous output but limited UART bandwidth (115 kbps) drops events under heavy context switching; interleaves trace data with shell output.
- **Thread runtime stats + shell sampling** — minimal firmware changes but sampled (not continuous), misses short-lived threads, and the sampling interval skews timing.

**Rationale:** The RAM buffer approach has zero runtime overhead on firmware timing (no UART blocking, no mutex contention), captures every event continuously, and works identically for local dev and CI. Post-mortem analysis is sufficient for the optimization workflow — live streaming is unnecessary.

### Trace format: 8-byte binary records

```c
#define TRACE_EVENT_SWITCHED_IN   0
#define TRACE_EVENT_SWITCHED_OUT  1
#define TRACE_EVENT_CREATE        2
#define TRACE_EVENT_ISR_ENTER     3
#define TRACE_EVENT_ISR_EXIT      4
#define TRACE_EVENT_IDLE          5

struct trace_record {
    uint32_t timestamp;      // k_cycle_get_32()
    uint8_t  event_type;     // one of TRACE_EVENT_*
    uint8_t  event_data;     // priority (for SWITCHED_IN), 0 otherwise
    uint16_t thread_id;      // assigned at creation (0xFFFF = ISR/IDLE)
};
// 8 bytes, no packing pragmas needed
```

At 4096 records (default, 32 KB buffer): ~4 seconds of continuous coverage at 1000 context switches/sec. Buffer size is configurable via Kconfig.

### Thread identification: sequential ID via THREAD_CUSTOM_DATA

Each `k_thread` stores its assigned ID in `custom_data` (enabled by `CONFIG_THREAD_CUSTOM_DATA=y`). On context switch in, the ID is read from `k_sched_current_thread_query()->custom_data` — O(1) lookup with no pointer chasing. A parallel static array `thread_names[MAX_THREADS][CONFIG_THREAD_MAX_NAME_LEN]` maps ID → name string (written at create time and dumped alongside records).

### Renode profiler: combined with trace

Run `cpu EnableProfilerPerfetto` and `cpu LogFunctionNames` simultaneously with the memory dump. Renode's profiler produces a standard Perfetto trace of function call stacks. The post-processor merges this with the thread timeline to produce a unified view.

### Post-processing: Python + Perfetto protobuf

A single Python script (`scripts/parse-trace.py`) reads the binary trace, the ELF file (for symbol resolution), and Renode's Perfetto trace. It outputs:
- `trace_combined.perfetto` — unified Perfetto trace with per-thread tracks + sched_switch events + call stacks
- `trace_report.txt` — human-readable summary (thread runtime %, context switches/sec, ISR latency stats)
- `trace_threads.json` — raw timeline for scripting

### CI: trace artifacts always published

The Renode CI job publishes trace artifacts (`trace.bin`, `trace_functions.perfetto`, `zephyr.elf`) with 7-day retention regardless of pass/fail. An optional parse-trace job generates a summary comment on PRs.

### Local dev: single-command workflow

A `/trace-and-analyze` script builds, runs, and post-processes in one command. The user opens `trace_combined.perfetto` in Perfetto UI (`ui.perfetto.dev`) for interactive inspection.

### Interactive results deployment: gh-pages

Deploy trace artifacts and test/coverage reports as static files on the `gh-pages` branch, following the existing `benchmark.yml` pattern. Three new directories:

| Directory | Content | Deep-link |
|-----------|---------|-----------|
| `dev/bench/` | (existing) ROM/RAM trend charts | — |
| `traces/<ref>/` | `trace_combined.perfetto`, `trace_report.txt` | `https://ui.perfetto.dev/#!/?url=https://<org>.github.io/weather-station/traces/<ref>/trace_combined.perfetto` |
| `tests/<ref>/` | `twister.xml`, `gcovr/` HTML coverage, `renode/` RF reports | Direct HTML links |

**Retention policy:**
- **Pull requests:** keep latest run only (overwrite `traces/pr-<N>/`, `tests/pr-<N>/`)
- **Master branch:** keep latest run only (overwrite `traces/master/`, `tests/master/`)
- **Size guard:** skip deployment if `trace_combined.perfetto` > 50 MB (warn in CI log)

**CI workflow:** A new `deploy-results` job (runs after `test-and-coverage` + `renode`) merges into the `gh-pages` branch and pushes, identical to how `docs` job works in `benchmark.yml`. Only runs on push to master and PRs — not on every branch push.

**Perfetto deep-link:** Perfetto UI supports loading traces from any URL via the `#!/?url=` hash fragment. Opening the deep-link URL in a browser loads the trace directly into Perfetto UI — no server-side rendering, no JavaScript bundling. The `.perfetto` file is served as a static binary asset from GitHub Pages.

## Architecture

```
┌───────────────────────────────────────────────────────────┐
│  Zephyr firmware (FRDM-MCXN947)                           │
│                                                           │
│  lib/trace_recorder/                                      │
│  ┌─────────────────────────────────────────────────────┐  │
│  │ CONFIG_TRACING + CONFIG_TRACING_USER                │  │
│  │ Overrides:                                          │  │
│  │   sys_trace_thread_switched_in_user()               │  │
│  │   sys_trace_thread_switched_out_user()              │  │
│  │   sys_trace_thread_create_user()                    │  │
│  │   sys_trace_thread_name_set_user()                  │  │
│  │   sys_trace_isr_enter_user()                        │  │
│  │   sys_trace_isr_exit_user()                         │  │
│  │   sys_trace_idle_user()                             │  │
│  │                                                     │  │
│  │ trace_records[4096]  8 bytes each = 32 KB (BSS)    │  │
│  │ thread_names[16][32] 16 threads × 32 chars         │  │
│  │                                                     │  │
│  │ Shell: trace status | dump | clear                  │  │
│  └─────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────┘
         │                              │
         │ sysbus ReadMemory            │ showAnalyzer + profiler
         ▼                              ▼
┌───────────────────────────────────────────────────────────┐
│  Renode (.resc + .robot)                                  │
│  ┌──────────────────────┐  ┌───────────────────────────┐  │
│  │ Memory dump          │  │ cpu EnableProfilerPerfetto│  │
│  │ sysbus ReadMemory    │  │ cpu LogFunctionNames      │  │
│  │ → trace.bin          │  │ → trace_functions.perfetto│  │
│  └──────────────────────┘  └───────────────────────────┘  │
└───────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌───────────────────────────────────────────────────────────┐
│  Post-processor (scripts/parse-trace.py)                  │
│                                                           │
│  Inputs:                                                  │
│    trace.bin + zephyr.elf + trace_functions.perfetto      │
│                                                           │
│  Processing:                                              │
│    1. Parse binary records → event stream                 │
│    2. Resolve thread names from ELF + names table          │
│    3. Build per-thread run intervals                       │
│    4. Generate Perfetto TrackEvent/sched_switch protos    │
│    5. Merge with Renode function profiler trace           │
│                                                           │
│  Outputs:                                                 │
│    trace_combined.perfetto    (interactive)               │
│    trace_report.txt           (human-readable)            │
│    trace_threads.json         (scriptable)                │
└───────────────────────────────────────────────────────────┘
         │
         ├─── Local dev: Perfetto UI (ui.perfetto.dev)
         │    — interactive thread timeline + flame graphs
         │
         └─── CI: deploy to gh-pages branch
              └── traces/<ref>/trace_combined.perfetto
              └── Perfetto deep-link:
                  ui.perfetto.dev/#!/?url=.../traces/pr-42/trace_combined.perfetto
```

```
lib/trace_recorder/
├── Kconfig                         # TRACE_RECORDER, buffer size, shell gate
├── CMakeLists.txt                  # Conditional source compilation
├── include/trace_recorder/
│   └── trace_recorder.h            # No public API (self-wiring via SYS_INIT)
└── src/
    ├── trace_recorder.c            # Overrides __weak _user hooks, ring buffer
    └── trace_recorder_shell.c      # trace dump, trace status, trace clear
```

## File manifest

| Action | File | Purpose |
|--------|------|---------|
| NEW | `lib/trace_recorder/Kconfig` | Library Kconfig: master toggle, buffer size (default 4096), shell gate |
| NEW | `lib/trace_recorder/CMakeLists.txt` | Conditional compilation gated on `CONFIG_TRACE_RECORDER` |
| NEW | `lib/trace_recorder/include/trace_recorder/trace_recorder.h` | Include guard (no public API) |
| NEW | `lib/trace_recorder/src/trace_recorder.c` | Override `__weak` tracing hooks, ring buffer, thread name table, SYS_INIT |
| NEW | `lib/trace_recorder/src/trace_recorder_shell.c` | Shell commands: `trace status`, `trace dump`, `trace clear` |
| NEW | `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf` | Kconfig fragment enabling tracing for Renode target |
| EDIT | `simulation/renode/frdm_mcxn947.resc` | Add profiler enable + function name logging + trace dump macros |
| NEW | `simulation/renode/tests/gateway_trace.robot` | Robot test: boot, reach shell, collect trace |
| EDIT | `simulation/renode/tests/common.robot` | Add `Dump Trace` keyword |
| NEW | `scripts/parse-trace.py` | Post-processor: binary trace → Perfetto + summary report |
| NEW | `scripts/trace-and-analyze` | One-command local dev workflow (build → Renode → parse → report) |
| EDIT | `.github/workflows/ci.yml` | Renode job: build with trace conf, publish trace artifacts, optional parse step; new `deploy-results` job for gh-pages push |
| EDIT | `.github/workflows/benchmark.yml` | Extend `docs` job (or add parallel job) to deploy `traces/` and `tests/` directories alongside existing docs |
| EDIT | `docs/backlog.md` | `[RENODE-TRACE-GH-PAGES]` item tracking Perfetto deep-link 404 guard and retention cleanup |

## Out of scope

- Live trace streaming (UART or other transport)
- Code coverage via Renode execution tracing (`CreateExecutionTracing`)
- Cache behavior analysis via Renode guest_cache tool
- Multi-node trace correlation (sensor node + gateway)
- Integration with Twister (deferred — Renode tests use Robot Framework, not Twister)
- Trace comparison / regression detection in CI (future: compare trace reports across PRs)
- `trace trigger` shell command (start/stop tracing at runtime — defer until needed)
