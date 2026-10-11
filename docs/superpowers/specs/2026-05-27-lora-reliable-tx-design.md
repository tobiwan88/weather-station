# LoRa Reliable Transmission & Protocol Completion — Design Spec

## Summary

Implement the four ADR-015 backlog items (RPC retry, FOTA windowing, session
persist, Ed25519 key cache) plus a sensor-side RPC/FOTA audit. A shared
`lora_pending.c` reliable TX subsystem provides ACK matching, retry with
randomized exponential backoff, and duty-cycle-aware timeout — consumed by
both RPC and FOTA. Async/fire-and-forget variants ensure zbus subscribers
never block.

## Constrained by

- ADR-015 (LoRa Protocol): frame format, 3 retries, SF-dependent timeout,
  windowed FOTA (4-16 chunks, 2s retry, 3 retries/chunk)
- ADR-008 (Kconfig composition): all features behind Kconfig guards
- ADR-002 (zbus IPC): no direct library-to-library calls; zbus listeners
  must not block or sleep
- ADR-003 (Q31 data model): sensor wire format unchanged
- ADR-014 (MCUboot FOTA): `fota_relay_transport_api` fulfilled via zbus channels

## Research foundation

Semtech, LoRaWAN spec v1.0.4/v1.1, ACM LoRaWAN retransmission literature
(ALR-LoRaWAN 2023, edge ACK 2024): confirm application-level retry with
randomized backoff is the correct pattern. Duty cycle (1% EU868) is the
hard ceiling on retry aggressiveness.

## Architecture

```
lib/lora_radio/src/lora_pending.c (NEW) — Shared Reliable TX
├── One pending slot per session (1:1 with lora_session[])
├── k_work_delayable per slot for retry timers
├── Single k_work_q for all retry work items
├── Randomized exponential backoff with duty-cycle floor
├── ACK matching via (node_id, seq_num) from RX thread
└── lora_rpc_result_chan — zbus channel for async results

lora_radio_rpc_send() → lora_pending_send() [blocking, for thread callers]
lora_radio_rpc_send_async() → lora_pending_send_async() [non-blocking, for zbus listeners]

RX thread switch extended to dispatch:
  LORA_FRAME_RPC_RESP (0x3) → lora_pending_ack_match()
  LORA_FRAME_ACK (0x8) → lora_pending_ack_match()
  LORA_FRAME_FOTA_CHUNK_ACK (0x5) → lora_pending_ack_match()
```

## Phases

### Phase 1 — Ed25519 Key Cache
- Import gateway Ed25519 key once in `lora_prov_init()`, cache `psa_key_id_t`
- Remove per-beacon `psa_import_key()`/`psa_destroy_key()` calls
- ~15 LOC net change in `lora_handler_prov.c`

### Phase 2 — `lora_pending.c` Shared Reliable TX
- Pending-request slot array (one per session)
- `k_work_delayable` retry loop with randomized backoff
- `lora_pending_send(node_id, seq_num, frame_type, tx_buf, tx_len)` — blocking
- `lora_pending_send_async(...)` — fire-and-forget, result on `lora_rpc_result_chan`
- `lora_pending_ack_match(node_id, seq_num)` — called from RX thread
- `lora_pending_init()` — creates workqueue
- Timeout scaling: SF7=5s, SF8=7s, SF9=10s, SF10=15s, SF11=22s, SF12=30s
  (base 5s × SF factor) with ±30% randomized jitter
- Exponential backoff: 1st retry=1×, 2nd=2×, 3rd=4× timeout
- Max 3 retries, then `-ETIMEDOUT`
- New Kconfig: `LORA_RADIO_RETRY_MAX=3`, `LORA_RADIO_RETRY_TIMEOUT_BASE_MS=5000`

### Phase 3 — RPC Retry
- `lora_radio_rpc_send()` rewritten to call `lora_pending_send()`, blocks on semaphore
- `lora_radio_rpc_send_async()` added — enqueues, returns immediately
- RX thread handles `LORA_FRAME_RPC_RESP` and `LORA_FRAME_ACK`
- Duplicate RPC detection on receiver side (check `last_seq_rx`, cache last response)
- New Kconfig: `LORA_RADIO_RPC_RETRY=y`, selects `LORA_RADIO_PENDING`
- `lora_rpc_result_chan` added to `lora_chan.h/.c`

### Phase 4 — FOTA Windowing
- Windowed sender: N chunks in flight (Kconfig default 8), driven by `k_work_delayable`
- Each chunk uses `lora_pending_send()` for per-chunk reliability
- Cooldown state machine: 3 consecutive timeouts → 30s pause → retry → fail
- Fix ACK offset bug in `lora_handle_fota_chunk`: report `fota_expected_offset` correctly
- Chunk ACK processing in RX thread (`LORA_FRAME_FOTA_CHUNK_ACK`)
- New Kconfig: `LORA_RADIO_FOTA_WINDOW=8`, `LORA_RADIO_FOTA_RETRY_MS=2000`,
  `LORA_RADIO_FOTA_COOLDOWN_S=30`

### Phase 5 — Session Persist After Pairing
- One-line addition: call `lora_session_persist()` after `lora_session_add()` in
  `lora_handle_prov_beacon()`
- The persistence machinery (`settings_save()` → settings handler → `lora_settings_export()`)
  already exists and works

### Phase 6 — RPC/FOTA Sensor-Side Audit
- Audit current RPC command coverage: PING, GET_VERSION, REBOOT implemented;
  GET_STATUS, SET_*, TRIGGER_SAMPLE, FOTA_START, FOTA_CANCEL deferred
- Deferred commands added as new backlog item
- Confirm dispatch table extensibility: adding commands = adding entry to `rpc_dispatch[]`

## Kconfig additions

```
config LORA_RADIO_PENDING
    bool "Reliable TX subsystem (retry + ACK matching)"

config LORA_RADIO_RETRY_MAX
    int "Max retries"
    default 3
    depends on LORA_RADIO_PENDING

config LORA_RADIO_RETRY_TIMEOUT_BASE_MS
    int "Base retry timeout (ms)"
    default 5000
    depends on LORA_RADIO_PENDING

config LORA_RADIO_RPC_RETRY
    bool "Enable RPC retry"
    default y
    select LORA_RADIO_PENDING

config LORA_RADIO_FOTA_WINDOW
    int "FOTA chunks in flight"
    default 8
    range 4 16
    depends on LORA_RADIO_FOTA

config LORA_RADIO_FOTA_RETRY_MS
    int "FOTA chunk retry timer (ms)"
    default 2000
    depends on LORA_RADIO_FOTA

config LORA_RADIO_FOTA_COOLDOWN_S
    int "Cooldown after 3 consecutive timeouts (s)"
    default 30
    depends on LORA_RADIO_FOTA
```

## New/removed backlog items

### Removed
- [ADR-015-LORA-RPC-RETRY] → Phase 3
- [ADR-015-LORA-FOTA-WINDOW] → Phase 4
- [ADR-015-LORA-PERSIST] → Phase 5
- [ADR-015-LORA-ED25519-CACHE] → Phase 1
- [Lora SENSOR] "Do we support RPC/FOTA commands?" → Phase 6

### Added
- [LORA-SECURITY-KEY-CACHE] Security review of cached Ed25519 key in PSA Crypto
- [LORA-RPC-COMMANDS] Implement remaining RPC commands (GET_STATUS, SET_*, TRIGGER_SAMPLE, FOTA_START/CANCEL)

## File change summary

| Phase | Files changed | New files | LOC delta |
|-------|-------------|-----------|-----------|
| 1 | `lora_handler_prov.c`, `lora_radio_main.c` | — | ~+15/−5 |
| 2 | `lora_radio_main.c`, `lora_radio_internal.h` | `lora_pending.c`, `lora_pending.h` | ~+200 |
| 3 | `lora_handler_rpc.c`, `lora_radio_main.c`, `lora_session.h`, `lora_chan.h`, `lora_chan.c` | — | ~+50/−20 |
| 4 | `lora_handler_fota.c`, `lora_radio_main.c` | — | ~+150/−50 |
| 5 | `lora_handler_prov.c` | — | +1 |
| 6 | — | — | — |
| Kconfig | `lib/lora_radio/Kconfig` | — | ~+30 |
