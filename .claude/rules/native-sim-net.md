---
paths:
  - "lib/http_dashboard/**"
  - "lib/mqtt_publisher/**"
  - "lib/pipe_transport/**"
  - "apps/gateway/**"
  - "tests/integration/**"
---

# native_sim networking gotchas

- Threads share one NSOS epoll fd. NSOS carries one local patch (`zephyr/patches.yml`, run
  `west patch apply`). CONFIG_ZVFS_POLL_MAX must cover 1 eventfd + listen sockets +
  HTTP_SERVER_MAX_CLIENTS (12 in the gateway; 3 crashed with parallel clients), and
  CONFIG_NET_MAX_CONTEXTS >= 16.
- Test parallel HTTP clients from the Mac via the container IP: loopback tests miss timing bugs.
- A crash with EPOLL_CTL_ADD EEXIST (errno 17) means two threads raced on socket open. If it recurs on v4.5, re-add only the EEXIST -> EPOLL_CTL_MOD retry in nsos_adapt_poll_add as a west patch (old patch 0001 is in git history before commit "chore(zephyr): migrate to Zephyr v4.5.0-rc1").
- Integration tests: leave ≥ 0.3 s between HTTP POSTs and ≥ 1.5 s after triggering
  background socket work. Prefer the harness `wait_for_*` helpers over fixed sleeps.
- Integration tests use the harness methods, never raw DUT strings. The DUT fixture is
  session-scoped, and tests that need login use the `authed_harness` fixture.
