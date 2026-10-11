# REQ-TIME — Time Synchronization Requirements

## Status
review

## Version
0.1

## Context
Wall-clock time comes from Zephyr's upstream `net_config` SNTP client (boot sync, async resync on the socket service). Consumers read `sys_clock_gettime(SYS_CLOCK_REALTIME)`.

## Functional Requirements

### REQ-TIME-001 — SNTP Boot Sync
The system shall perform an SNTP time synchronization at boot, before any timestamped operations begin, via `CONFIG_NET_CONFIG_CLOCK_SNTP_INIT` (SYS_INIT APPLICATION prio 95).

### REQ-TIME-002 — Periodic SNTP Resync
The system shall perform periodic SNTP resynchronization to correct clock drift, via `CONFIG_NET_CONFIG_SNTP_INIT_RESYNC`. The resync interval shall be configurable via Kconfig.

### REQ-TIME-003 — Runtime Server and Resync
The dashboard `action=sntp_resync` and `sntp_server` settings shall call `net_config_sntp_set_server()` (`CONFIG_NET_CONFIG_SNTP_INIT_SERVER_RUNTIME`), which starts an immediate resync.

### REQ-TIME-004 — Realtime Clock as Time Source
Consumers shall read wall-clock time with `sys_clock_gettime(SYS_CLOCK_REALTIME)`. The project shall provide no time API of its own.

### REQ-TIME-005 — Timestamp Contract
The system shall use millisecond granularity for all timestamps. Timestamps shall be epoch milliseconds (Unix time * 1000).

### REQ-TIME-007 — Clock Display Update
The system shall update the display clock every 60 seconds, reading wall-clock time from the realtime clock (`sys_clock_gettime(SYS_CLOCK_REALTIME)`).

## Dependencies
- REQ-HTTP: dashboard resync action (`sntp_resync`) and SNTP server setting
- REQ-DISPLAY-002: Analog clock display

## Constraints
- Test pacing: ≥ 1.5s after triggering background socket work
- Timestamp granularity: milliseconds

## Acceptance Criteria
- [ ] [native_sim] Integration: SNTP sync completes at boot
- [ ] [native_sim] Integration: `sys_clock_gettime(SYS_CLOCK_REALTIME)` returns valid time after sync
- [ ] [native_sim] Integration: Dashboard resync action triggers immediate SNTP query
- [ ] [native_sim] Integration: Clock display updates every 60 seconds
- [ ] [renode] Integration: SNTP sync over simulated network
- [ ] [hil] Board: SNTP sync with external NTP server
- [ ] [hil] Board: Clock drift corrected by periodic resync

## Related ADRs
- ADR-008: Kconfig-Only App Composition (SYS_INIT priority ordering)

## Related Zephyr subsystems
- CONFIG_SNTP, CONFIG_NET_CONFIG_CLOCK_SNTP_INIT, CONFIG_NET_CONFIG_SNTP_INIT_RESYNC, CONFIG_NET_CONFIG_SNTP_INIT_SERVER_RUNTIME, CONFIG_NET_SOCKETS_SERVICE
