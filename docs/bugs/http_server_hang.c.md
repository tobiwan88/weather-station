# HTTP Server Hang Bug Analysis

**Date**: April 2026
**Status**: NSOS SIGSEGV bugs fixed (32/33 tests passing). One HTTP server hang bug remains.

---

## Bug: `test_token_rotation_invalidates_old_token` — HTTP Server Hang on Multiple POSTs

**Symptom**: After token rotation, the second POST request times out (`ReadTimeout: HTTPConnectionPool Read timed out`).

### Test Results

| Test Suite | Passing | Failing |
|-----------|---------|---------|
| smoke | 4/4 | 0 |
| shell | 14/14 | 0 |
| http | 21/23 | 2 (`test_token_rotation_invalidates_old_token`, `test_two_back_to_back_posts_different_tokens`) |
| **Total** | **32/33** | **1 (scope is 2 http tests)** |

---

## What We Know

### Test Observations

1. **Single POST requests work fine** — any isolated POST returns 200/401 correctly
2. **Multiple POSTs in sequence cause hang** — second POST never receives a response
3. **Server is completely dead after first POST** — no logs, no response, just timeout
4. **The hang is NOT specific to token rotation** — `test_two_back_to_back_posts_different_tokens` (no rotation) also fails
5. **Delay does NOT help** — 3 second delay between POSTs doesn't fix it

### Log Sequence (Failing Case)

```
12:04:34 INFO [http] _post: final headers = {'Authorization': 'Bearer 6daa0ff291f7b122bbdf53c48e6108f9'}
12:04:34 DEBUG [urllib3.connectionpool] Starting new HTTP connection (1): localhost:8080
12:04:39 ERROR [http] POST http://localhost:8080/api/config connection error after 5.007s: Read timed out
```

**Note**: No device logs at all for the second POST — server never even receives it or processes it.

### Log Sequence (Passing Case - Single POST)

```
12:04:31 DEBUG [urllib3.connectionpool] http://localhost:8080 "POST /api/config HTTP/1.1" 401 None
12:04:31 DEBUG [http] POST /api/config ['trigger_interval_ms'] → 401 (0.160s)
12:04:31 INFO [device] http_dashboard: auth_check: comparing presented=28312fc0... with s_token=6daa0ff...
```

The server logs appear immediately when there's a response.

---

## Root Cause Hypothesis

**The issue is in the Zephyr HTTP server's connection handling, NOT in:**
- Token rotation logic (auth works correctly)
- Header capture (works correctly)
- NSOS adapter (SIGSEGV bugs are fixed)

**Most likely cause**: The HTTP server's internal connection state machine is getting stuck after handling certain request patterns. The first POST (whether 401 or 200) causes the server's per-connection state to become corrupted such that it stops processing new connections.

**Key observation**: The server doesn't just fail to respond to the second POST — it stops accepting ANY new connections. This suggests the HTTP server's listen socket or epoll state is being corrupted.

---

## What is NOT the Issue

1. **NOT NSOS epoll bugs** — EEXIST and TOCTOU races are fixed with patches
2. **NOT token rotation** — the same hang happens WITHOUT token rotation
3. **NOT a timing issue** — 3s delay doesn't help
4. **NOT connection pool exhaustion in Python** — using fresh Session per request
5. **NOT header capture buffer overflow** — buffer is sized at 64 bytes, headers are < 64 chars
6. **NOT a spinlock issue** — k_spinlock patches are working

---

## Debugging Steps Completed

### 1. Enhanced Test Logging (Implemented ✅)
Added `device_logger.drain()` at key points in tests to see server logs during test execution. This confirmed that server logs DO appear for successful requests (first POST shows auth_check, respond_401, etc.) but NO server logs appear for the hung POST.

### 2. HTTP Server Logging (Implemented ✅)
Added `LOG_INF`/`LOG_DBG` throughout `auth.c` and `http_dashboard.c` to trace handler entry/exit. Logs appear for first POST but NOT for second POST.

### 3. Isolation Tests Created
Created `test_two_back_to_back_posts_different_tokens` to isolate the issue. Confirmed it's about multiple consecutive POSTs, not specifically token rotation.

---

## Next Steps (Backlog Item B - Automatic Live Device Logging)

Create a fixture that continuously drains device logs during test execution (not just in teardown) so we can see exactly what the server is doing at each moment. This would help identify where exactly the server stops responding.

---

## Files Modified During Debugging

### `lib/http_dashboard/src/auth.c`
Added debug logging:
- `auth_token_rotate()` — logs lock acquisition, token generation, unlock
- `auth_check()` — logs token comparison with full values (presented vs s_token)

### `lib/http_dashboard/src/http_dashboard.c`
Added debug logging:
- `api_config_handler()` — LOG_INF at entry showing method/status/data_len
- `respond_401()` — LOG_INF at entry and exit showing status/body_len/final_chunk

### `tests/integration/pytest/test_http_api.py`
Added step logging and `device_logger.drain()` calls at key points to flush server logs during test execution.

---

## Related Files

- `lib/http_dashboard/src/auth.c` — token management and auth check
- `lib/http_dashboard/src/http_dashboard.c` — HTTP request handler
- `lib/http_dashboard/src/process_post.c` — POST body processing
- `tests/integration/pytest/test_http_api.py` — failing test
- `tests/integration/pytest/harnesses/http_harness.py` — HTTP client

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

---

## Reminder to User

You mentioned "using a live client the thing seems stable and not many hangs". This suggests:
1. The issue may be specific to how the test harness interacts with the server
2. Or the issue is timing-related and live clients naturally pace their requests differently
3. Possible that the test's `requests.Session()` with short delays between requests creates a specific pattern that triggers the bug
