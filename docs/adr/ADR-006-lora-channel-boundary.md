# ADR-006 — LoRa Module Channel Boundary

| Field | Value |
|-------|-------|
| **Status** | **Deferred** |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

> **Note:** This ADR documents a **design decision for a feature that has not been implemented**. No `lib/lora_radio/` code exists. The `loramac-node` west module is listed in `west.yml` but has never been fetched. This document is kept as a reference for when LoRa support is added in Phase 3 (real hardware).

---

## Context

The system plans to use LoRa point-to-point (no LoRaWAN) for the outdoor sensor node to transmit weather data to the gateway. The questions this ADR answers are:

1. Should the LoRa channel carry raw packet bytes, a LoRa-specific struct, or plain `env_sensor_data` events?
2. Should consumers know whether a reading came from LoRa or a local sensor?
3. Where do RSSI, SNR, and node sequence number information belong?

---

## Decision

When implemented, `lib/lora_radio/` will be a **bounded context**. Everything LoRa-specific — packet framing, RSSI, SNR, node IDs, sequence numbers, radio configuration — stays inside the library. Only `env_sensor_data` events cross the module boundary onto `sensor_event_chan`. Link diagnostics travel on a **separate** `lora_link_chan`.

### Boundary principle

```
lib/lora_radio/ (bounded context)
├── Radio driver (SX1276/SX1262)
├── Packet framing (internal)
├── RSSI, SNR, seq_num (internal)
│
├── publishes env_sensor_data ×N → sensor_event_chan
└── publishes lora_link_info     → lora_link_chan (future)

Consumers (display, MQTT, logger) receive env_sensor_data
with no knowledge of transport origin.
```

### Key design choices (when implemented)

- **One LoRa packet → N `env_sensor_data` events.** Each sample in a packet becomes an individual event on `sensor_event_chan`.
- **Timestamps use the gateway's local clock**, not the sensor node's clock.
- **`lora_link_chan`** carries RSSI, SNR, node_id, seq_num for diagnostics. Initially only the logger subscribes.
- **Radio configuration** is defined in a single shared header that both TX and RX nodes include.
- **Gateway pre-knows remote sensor UIDs** — no auto-discovery protocol.

### Constraints

- EU868 1% duty cycle: at SF10/BW125, minimum transmit interval ~37 seconds. Poll interval must be ≥ 60s.
- Max packet ~40 bytes (4 samples) — well within LoRa 255B limit.

---

## Consequences

**Easier:**
- Replacing the radio (SX1276 → SX1262, or LoRaWAN) requires changing only `lib/lora_radio/`.
- Display and MQTT work identically regardless of transport origin.

**Harder:**
- Gateway must pre-know remote sensor UIDs.
- Packet loss results in stale display data.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Expose raw LoRa bytes on a channel | Consumers must parse LoRa packets — couples them to LoRa internals |
| Merge RSSI/SNR into `env_sensor_data` | Radio-specific fields make no sense for local sensors |
| LoRaWAN (TTN/ChirpStack) | Requires gateway hardware and network server; adds complexity for a home LAN system |

---

## Related

- ADR-002 — zbus as system bus
- ADR-009 — native_sim first (LoRa is Phase 3)
- Backlog: `[RENODE-PHASE2]`, `[SERIALIZATION]`
