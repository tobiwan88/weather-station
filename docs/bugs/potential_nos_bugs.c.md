# native_sim NSOS Bug Analysis

**Date**: April 2026
**Status**: SIGSEGV/crash bugs fixed. 32/33 integration tests passing. One HTTP server hang bug remains (test `test_token_rotation_invalidates_old_token`).

---

## System Context

On `native_sim/native/64`, all Zephyr sockets share **one OS-level epoll fd** managed by
`nsos_adapt.c`. Zephyr threads (HTTP server, MQTT, SNTP) are POSIX threads coordinated by
the NCE (Native Co-operating Engine) semaphore model — only ONE Zephyr thread runs at a
time, but preemption between them can happen at any yield point.

The shared `nsos_polls` doubly-linked list (`sys_dlist_t` in `nsos_sockets.c`) tracks
active poll descriptors.

---

## Fixed Bugs ✅

### Bug 1 — EEXIST Crash in `nsos_adapt_poll_add`

**Symptom**: DUT exits with code 1, log ends with:
```
error in EPOLL_CTL_ADD: errno=17
```

**Root Cause**: An fd is closed and reopened before the previous `EPOLL_CTL_DEL`
completes. The new `EPOLL_CTL_ADD` fails with `EEXIST` because the stale entry is still
registered.

**Fix (patch 0001)**: On `EEXIST`, use `EPOLL_CTL_MOD` to update the stale entry.
On `ENOENT` in `poll_remove`, silently ignore (Linux auto-cleans on `close()`).

---

### Bug 2 — `epoll_ctl` TOCTOU Race

**Root Cause**: `nsos_adapt_poll_add()` and `nsos_adapt_poll_remove()` called
`epoll_ctl` without a lock, causing races when multiple Zephyr threads operated concurrently.

**Fix (patch 0001)**: Static `pthread_mutex_t nsos_epoll_mutex` wraps the full
`epoll_ctl` sequence in both functions.

---

### Bug 3 — SIGSEGV in `sys_dlist_remove`

**Symptom**: DUT exits with SIGSEGV (signal 11). Backtrace shows:
```
sys_dlist_remove (node=0x4b47a8 <kheap.system_heap+688>)
  prev->next = next;  // prev was NULL
```

**Root Cause (two bugs in `nsos_sockets.c`)**:

1. **No lock around `nsos_polls` dlist** — `nsos_poll_prepare()` and
   `nsos_poll_update()` accessed the shared dlist without synchronisation.
   Zephyr could preempt between `is_linked` check and `sys_dlist_remove`,
   causing a node with `prev == NULL` → SIGSEGV.

2. **`nsos_close()` use-after-free** — called `k_free(sock)` without removing
   `sock->poll.node` from `nsos_polls`. A subsequent `nsos_poll_update()` on
   that slot accessed freed memory.

**Why `k_spinlock` (not `pthread_mutex`)**: `pthread_mutex` does not block NCE
context switches on native_sim. Only `k_spin_lock()` (via `arch_irq_lock`) blocks
the NCE preemption. This was confirmed empirically — `pthread_mutex` did not fix
the SIGSEGV.

**Fix (patch 0002)**: `k_spinlock` around all `nsos_polls` operations:
- `nsos_close`: atomically check-and-remove node under lock; callback outside lock
- `nsos_poll_prepare`: lock around `is_linked` check + `sys_dlist_remove`; remove before re-add
- `nsos_poll_update`: lock around `is_linked` + `sys_dlist_remove`

---

## Remaining Bug 🔴

### `test_token_rotation_invalidates_old_token` — HTTP Server Hang

**Symptom**: After token rotation, the second POST request times out (`ReadTimeout: HTTPConnectionPool Read timed out`).

**Behaviour**:
1. Old token POST → 401 (correct)
2. New token POST → times out after 5s (server hangs, does not respond)

**Current evidence**:
- DUT does NOT exit — process continues running
- Log shows the POST was received and auth was checked (header captured correctly)
- No SIGSEGV, no crash — server simply stops responding to POST requests
- This happens ONLY after token rotation; normal POST requests work fine

**Hypothesis**: The token rotation changes `s_token` under a spinlock, but there may be a window where the HTTP handler sees a partially-written token. However, the auth check is using constant-time comparison and the token is 32 hex chars — this shouldn't cause a hang.

**What is NOT the issue**:
- SIGSEGV is eliminated (k_spinlock patches work)
- ConnectionResetError is eliminated (epoll patches work)
- Header capture is working (401 returned correctly for old token)

**To diagnose**:
1. Increase HTTP server debug logging (`CONFIG_NET_HTTP_SERVER_LOG_LEVEL_DBG=y`)
2. Add logging in `auth.c` around the token comparison and rotation
3. Use the GDB stub below to attach at the point of hang

---

## Patch Files

In `zephyr/patches/zephyr/`, applied by `west patch apply`:

| Patch | File | What it fixes |
|-------|------|---------------|
| `0001-drivers-net-nsos-fix-epoll-race-and-poll-dlist-corru.patch` | `nsos_adapt.c` + `nsos_sockets.c` | EEXIST fix + pthread_mutex for epoll_fd |
| `0002-drivers-net-nsos-use-k_spinlock-for-nsos_polls-prote.patch` | `nsos_sockets.c` | k_spinlock for nsos_polls dlist |

---

## GDB Stub for Debugging

File: `tests/integration/pytest/crash_watch.gdbinit`

```bash
# Attach GDB to running native_sim process
gdb --batch -p <pid> -x crash_watch.gdbinit
```

The script sets breakpoints on:
- `nsi_print_error_and_exit` — NSOS fatal errors
- `z_fatal_error` — Zephyr kernel fatal handler
- `catch signal SIGABRT` / `catch signal SIGSEGV`
- `nsos_adapt_poll_remove` entry
- `nsos_sockets.c:346` and `nsos_sockets.c:359` — dlist operations

When attached, it captures `bt full`, `info threads`, and memory dumps of the poll node.

---

## Test Results

| Test Suite | Passing | Failing |
|-----------|---------|---------|
| smoke | 4/4 | 0 |
| shell | 14/14 | 0 |
| http | 21/22 | 1 (`test_token_rotation_invalidates_old_token`) |
| **Total** | **32/33** | **1** |

---

## Upstreaming Notes

Both patches are upstreamable:
- 0001: EEXIST handling + epoll mutex — fixes real races on multi-socket native_sim
- 0002: k_spinlock for nsos_polls — fixes dlist corruption on concurrent socket close/poll

The remaining HTTP hang bug is likely in `http_dashboard` auth code, not NSOS.
