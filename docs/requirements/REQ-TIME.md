# REQ-TIME — Time Synchronization Requirements

## Status
review

## Version
0.1

## Context
Time synchronization provides wall-clock time via SNTP for timestamping sensor events, display clock, and coordinated operations. The system syncs at boot and periodically, with resync on config command.

## Functional Requirements

### REQ-TIME-001 — SNTP Boot Sync
The system shall perform an SNTP time synchronization at boot, before any timestamped operations begin.

### REQ-TIME-002 — Periodic SNTP Resync
The system shall perform periodic SNTP resynchronization to correct clock drift. The resync interval shall be configurable via Kconfig.

### REQ-TIME-003 — Config-Command Resync
The system shall accept time resync requests via `config_cmd_chan`, triggering an immediate SNTP query.

### REQ-TIME-004 — Single Authoritative Time Source
The system shall provide a single authoritative `get_epoch_ms()` function for all timestamp needs. The synced flag shall be atomic — written once after first successful NTP query, read frequently.

### REQ-TIME-005 — Timestamp Contract
The system shall use millisecond granularity for all timestamps. Timestamps shall be epoch milliseconds (Unix time * 1000).

### REQ-TIME-006 — Native_sim Pre-Sync Delay
The system shall add a presync delay of `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS` (default 200ms) in native_sim test builds before opening SNTP sockets, to avoid EPOLL_CTL_ADD EEXIST crashes.

### REQ-TIME-007 — Clock Display Update
The system shall update the display clock every 60 seconds, reading wall-clock time from `sntp_sync`.

### REQ-TIME-008 — SYS_INIT Priority
The system shall initialize sntp_sync at SYS_INIT priority 80 (earliest, before timestamps are needed).

## Dependencies
- REQ-CONFIG-001: config_cmd channel for resync requests
- REQ-DISPLAY-002: Analog clock display

## Constraints
- `CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS=200` in test builds
- Test pacing: ≥ 1.5s after triggering background socket work
- Timestamp granularity: milliseconds

## Acceptance Criteria
- [ ] [native_sim] Integration: SNTP sync completes at boot
- [ ] [native_sim] Integration: get_epoch_ms() returns valid time after sync
- [ ] [native_sim] Integration: Config-command resync triggers immediate SNTP query
- [ ] [native_sim] Integration: Presync delay prevents EPOLL_CTL_ADD EEXIST
- [ ] [native_sim] Integration: Clock display updates every 60 seconds
- [ ] [renode] Integration: SNTP sync over simulated network
- [ ] [hil] Board: SNTP sync with external NTP server
- [ ] [hil] Board: Clock drift corrected by periodic resync

## Related ADRs
- ADR-008: Kconfig-Only App Composition (SYS_INIT priority ordering)

## Related Zephyr subsystems
- CONFIG_SNTP
- CONFIG_NET_UDP
- CONFIG_NET_SOCKETS
- CONFIG_SETTINGS
- CONFIG_DATE_TIME
