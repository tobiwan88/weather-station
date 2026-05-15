# ADR-016 — Thread Trace Recorder and Renode Performance Analysis

| Field | Value |
|-------|-------|
| **Status** | Proposed |
| **Date** | 2026-05-15 |
| **Deciders** | Tobi |

---

## Context

The FRDM-MCXN947 gateway runs a multi-threaded Zephyr application (SNTP sync,
MQTT publisher, remote sensor manager, HTTP dashboard server, system workqueue,
etc.). Without thread-level performance visibility, diagnosing issues like
thread starvation, priority inversion, ISR latency, or scheduling inefficiency
requires guesswork — there is no way to see which threads ran, for how long,
or what consumed CPU time.

The Zephyr tracing subsystem (`CONFIG_TRACING` + `CONFIG_TRACING_USER`)
provides thread-switch, ISR, idle, and kernel-object hooks but was unused.
Renode offers guest profiling (Perfetto flame graphs), function name logging,
and peripheral access logging natively. The project already had Renode
integration (`simulation/renode/`) with Robot Framework tests for boot and
FOTA, but no tracing or profiling was enabled.

---

## Decision

Use `CONFIG_TRACING_USER` with a new `lib/trace_recorder/` library that
overrides Zephyr's `__weak` tracing hooks to write compact 8-byte binary
records to a static ring buffer, then dump the buffer post-mortem via Renode
`sysbus ReadMemory`. Combine this with Renode's built-in Perfetto profiler
for unified thread-level + function-level analysis.

---

## Consequences

**Easier:**
- Thread starvation, priority inversion, and ISR latency are directly
  observable in Perfetto UI
- Zero runtime overhead on firmware timing (no UART blocking, no mutex
  contention, no dynamic allocation)
- Works identically in local dev and CI — no hardware dependency
- Single-command workflow (`trace-and-analyze`) builds, runs, and
  post-processes

**Harder:**
- Trace size is bounded by static buffer (32 KB default) — captures ~4
  seconds at 1000 ctx switches/sec; no live streaming
- Thread identification requires `CONFIG_THREAD_CUSTOM_DATA=y`, adding a
  pointer-sized field to every `k_thread`
- Post-processing requires ELF + Python tooling — not inspectable directly
  on-device

**Constrained:**
- The trace buffer format (8-byte records) is a stable contract between
  firmware and `parse-trace.py`; changing it requires coordinated updates
- Trace recorder depends on `CONFIG_TRACING=y` in the kernel — cannot run
  without Zephyr tracing subsystem enabled
- Thread ID namespace is limited to 16 (default) to 64 (configurable) threads

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| UART streaming backend | Limited UART bandwidth (115 kbps) drops events under heavy context switching; interleaves trace data with shell output; adds blocking I/O in tracing hooks |
| Thread runtime stats + shell sampling | Minimal firmware changes but sampled (not continuous), misses short-lived threads, and the sampling interval skews timing results |
| RTT/Segger-based tracing | Requires J-Link hardware and Segger tooling; not usable in CI or Renode; couples the solution to a specific debug probe vendor |

---

## See also

- Current implementation: `lib/trace_recorder/`, `simulation/renode/`,
  `scripts/parse-trace.py`, `scripts/trace-and-analyze`
- Architecture docs: `docs/architecture/trace-recorder.md`,
  `docs/architecture/renode-trace-workflow.md`
- Related ADRs: ADR-008 (Kconfig composition), ADR-009 (native_sim first),
  ADR-012 (integration test architecture)
