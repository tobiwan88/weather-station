# native_sim NSOS Crash Analysis

**Date**: April 2026
**Symptom**: `test_trigger_interval_bounds_accepted` crashes DUT with SIGSEGV (exit -11)
**Status**: Root cause confirmed. Three patches applied. Tests passing after 0003.

---

## System Context

On `native_sim/native/64`, all Zephyr sockets share **one OS-level epoll fd** managed by
`nsos_adapt.c`. Zephyr threads (HTTP server, MQTT, SNTP) are POSIX threads coordinated by
the NCE (Native Co-operating Engine) semaphore model — only ONE Zephyr thread runs at a
time, but preemption between them can happen at any yield point.

The shared `nsos_polls` doubly-linked list (`sys_dlist_t` in `nsos_sockets.c`) tracks
active poll descriptors. No lock protected it.

---

## Bug 1 — EEXIST Crash in `nsos_adapt_poll_add` ✅ Fixed (patch 0001)

### Symptom
DUT exits with code 1, log ends with:
```
error in EPOLL_CTL_ADD: errno=17
```

### Root Cause
An fd is closed and reopened (reused by the OS) before the previous `EPOLL_CTL_DEL`
completes. The new `EPOLL_CTL_ADD` fails with `EEXIST` because the stale entry is still
registered. `nsos_adapt_poll_add()` called `nsi_print_error_and_exit()` on any error.

### Fix — `nsos_adapt.c` (patch 0001)
On `EEXIST`, remove the stale entry and re-add. On `ENOENT` in `poll_remove`, silently
ignore (Linux auto-cleans the epoll entry on `close()`).

---

## Bug 2 — `epoll_ctl` TOCTOU Race ✅ Fixed (patch 0002)

### Root Cause
`nsos_adapt_poll_add()` and `nsos_adapt_poll_remove()` are called from different Zephyr
threads with no lock around `epoll_ctl`. The DEL+re-ADD sequence in the Bug 1 fix
introduced a window where a concurrent `poll_remove` could observe `ENOENT` on the
entry it just deleted, or a concurrent `poll_add` could see `EEXIST` on the entry the
first thread just re-added.

### Fix — `nsos_adapt.c` (patch 0002)
Static `pthread_mutex_t nsos_epoll_mutex` (PTHREAD_MUTEX_INITIALIZER) wraps the full
`epoll_ctl` sequence (including the DEL+re-ADD retry) in both `poll_add` and `poll_remove`.

---

## Bug 3 — SIGSEGV in `sys_dlist_remove` on `http_server_tid` ✅ Fixed (patch 0003)

### Symptom
DUT exits with SIGSEGV (signal 11). Identified via **GDB host-attach**:

```
gdb --batch -p <pid> -x crash_watch.gdbinit
```

GDB output:
```
Catchpoint 4 (signal SIGSEGV)
Thread 4 "http_server_tid" hit Catchpoint 4 (signal SIGSEGV),
sys_dlist_remove (node=0x4b47a8 <kheap.system_heap+688>)
  at zephyr/include/zephyr/sys/dlist.h:532
532     prev->next = next;
```

### How GDB was integrated

A new pytest fixture `gdb_crash_watcher` (in `conftest.py`) attaches GDB to the
native_sim process before the test body runs and reads the crash log in teardown:

```python
gdb_proc = subprocess.Popen([
    "gdb", "--batch", "-p", str(pid),
    "-ex", "set logging file gdb_crash.log",
    "-x", str(gdbinit),
])
```

The `.gdbinit` sets breakpoints on `nsi_print_error_and_exit`, `z_fatal_error`,
`catch signal SIGABRT`, and `catch signal SIGSEGV`, each running `bt full` + `info threads`.

### Root Cause — Two bugs in `nsos_sockets.c`

**1. No lock around `nsos_polls` dlist.**

`nsos_poll_prepare()` calls `sys_dlist_append(&nsos_polls, &poll->node)` and
`nsos_poll_update()` calls `sys_dnode_is_linked` + `sys_dlist_remove(&poll->node)`.
These run from different Zephyr threads with no synchronisation. Zephyr can preempt
between the `is_linked` check and the `sys_dlist_remove` call, letting another thread
modify the list first. The result: a node with `next != NULL` (sentinel) but `prev == NULL`
(zeroed by the other thread's `sys_dlist_remove` → `sys_dnode_init`). Then this thread
calls `sys_dlist_remove` and dereferences `prev` → SIGSEGV.

**2. `nsos_close()` use-after-free.**

The original `nsos_close()` iterated `nsos_polls` via `SYS_DLIST_FOR_EACH_CONTAINER`
to signal `ZSOCK_POLLHUP` but never called `sys_dlist_remove`. It then called
`k_free(sock)`. The freed `sock->poll.node` remained in the dlist. A subsequent
`nsos_poll_update()` call on that slot would access freed memory, producing the same
corrupted-node state.

### Why `k_spinlock` is correct here

An earlier analysis concluded that `k_spinlock` (via `arch_irq_lock`) is "a no-op for
cross-thread exclusion on native_sim". This is **incorrect**.

The NCE engine uses POSIX semaphores to ensure only ONE Zephyr thread runs at a time.
A Zephyr context switch (preemption) is triggered only when the current thread calls
`nce_halt_cpu()` and the HW model calls `nce_wake_cpu()` on the next thread. `irq_lock()`
blocks the HW model from triggering that switch. Therefore `k_spin_lock` prevents
preemption between Zephyr threads and provides correct mutual exclusion on single-core
native_sim — no `pthread_mutex` needed in `nsos_sockets.c`.

### Fix — `nsos_sockets.c` (patch 0003)

```c
static struct k_spinlock nsos_polls_lock;
```

- `nsos_poll_prepare`: hold spinlock around `sys_dlist_append`
- `nsos_poll_update`: hold spinlock around `sys_dnode_is_linked` + `sys_dlist_remove`
  (capture `was_linked` under lock; call `nsos_adapt_poll_remove` outside)
- `nsos_close`: hold spinlock to atomically check-and-remove the node, then call
  the `POLLHUP` callback and `k_free` outside the lock

---

## What Was Tried

| Attempt | Result |
|---------|--------|
| Ignored EEXIST/ENOENT in `nsos_adapt.c` (patch 0001) | Fixed Bug 1 (DUT no longer exits on errno=17) |
| `pthread_mutex` around `epoll_ctl` in `nsos_adapt.c` (patch 0002) | Fixed Bug 2. Crash (SIGSEGV) persisted — wrong location |
| GDB host-attach via `gdb_crash_watcher` pytest fixture | Identified exact crash site: `sys_dlist_remove` with `prev=NULL` |
| `k_spinlock` around `nsos_polls` dlist + `nsos_close` node removal (patch 0003) | Fixed Bug 3. Full suite passes. |

---

## Patch Files

In `zephyr/patches/zephyr/`, applied in order by `west patch apply`:

| Patch | File | What it fixes |
|-------|------|---------------|
| `0001-nsos-adapt-use-EPOLL_CTL_MOD-on-EEXIST-in-poll-add.patch` | `nsos_adapt.c` | EEXIST crash + ENOENT ignored |
| `0002-drivers-net-nsos-add-pthread-mutex-around-epoll-ctl.patch` | `nsos_adapt.c` | `epoll_ctl` TOCTOU race |
| `0003-drivers-net-nsos-protect-nsos_polls-dlist-with-spinlock.patch` | `nsos_sockets.c` | dlist race + use-after-free in `nsos_close` |

---

## Diagnostic Artefacts

- `tests/integration/pytest/crash_watch.gdbinit` — GDB script with breakpoints on crash entry points
- `tests/integration/pytest/conftest.py` — `gdb_crash_watcher` fixture (opt-in, used by `test_trigger_interval_bounds_accepted`)
- GDB crash output appears in `handler.log` under `ERROR [conftest] GDB crash log` when a crash is captured

---

## Upstreaming Notes

All three patches are upstreamable to Zephyr mainline:
- 0001/0002 address real epoll races that can occur on any multi-socket native_sim workload
- 0003 addresses a missing synchronisation primitive and a use-after-free that affect any
  Zephyr application using NSOS sockets with concurrent open/close and poll operations
