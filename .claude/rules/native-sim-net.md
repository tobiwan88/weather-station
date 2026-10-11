---
paths:
  - "lib/http_dashboard/**"
  - "lib/mqtt_publisher/**"
  - "lib/sntp_sync/**"
  - "lib/pipe_transport/**"
  - "apps/gateway/**"
  - "tests/integration/**"
---

# native_sim networking gotchas

- All threads share one NSOS epoll fd without a mutex. `CONFIG_ZVFS_POLL_MAX` must be ≥ 8 and
  `CONFIG_NET_MAX_CONTEXTS` ≥ 16.
- A crash with `EPOLL_CTL_ADD` `EEXIST` (errno 17) means two threads opened sockets at the
  same moment. Background threads sleep `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS` after waking,
  before opening sockets (200 ms in test builds).
- Integration tests: leave ≥ 0.3 s between HTTP POSTs and ≥ 1.5 s after triggering
  background socket work. Prefer the harness `wait_for_*` helpers over fixed sleeps.
- Integration tests use the harness methods, never raw DUT strings. The DUT fixture is
  session-scoped, and tests that need login use the `authed_harness` fixture.
