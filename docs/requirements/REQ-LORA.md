# REQ-LORA — LoRa Protocol Requirements

## Status
review

## Version
0.1

## Context
LoRa provides long-range wireless communication between outdoor sensor nodes and the gateway. The protocol uses a point-to-point star topology with AES-128-GCM encryption, button-press pairing, and supports data, FOTA, RPC, and provisioning traffic over a unified frame format.

## Functional Requirements

### REQ-LORA-001 — Fixed L2 Header Format
The system shall use an 8-byte fixed L2 header for all LoRa frames, followed by a 12-byte AES-128-GCM authentication tag. Total frame overhead: 20 bytes.

### REQ-LORA-002 — Star Topology
The system shall operate in a point-to-point star topology with one gateway and up to 256 sensor nodes.

### REQ-LORA-003 — AES-128-GCM Encryption
The system shall encrypt all LoRa payloads using AES-128-GCM with per-session keys. The 12-byte GCM tag shall be appended to each frame.

### REQ-LORA-004 — Button-Press Pairing
The system shall support runtime node pairing via button-press authentication with Ed25519 gateway signature. The pairing window shall be 30 seconds.

### REQ-LORA-005 — Compact Wire Reading Format
The system shall encode sensor readings for LoRa transmission as 5 bytes (1 byte type + 4 bytes Q31), achieving 4x reduction from the full 20-byte `env_sensor_data` struct.

### REQ-LORA-006 — Unified Frame Types
The system shall support 16 frame type slots (0x0–0xF), with at least 6 reserved for: DATA, FOTA_CHUNK, FOTA_ACK, RPC_CMD, RPC_RESP, PROV_BEACON, PROV_RESP.

### REQ-LORA-007 — RPC Retry Logic
The system shall implement ACK-based RPC delivery with up to 3 retries and SF-dependent timeout (5s at SF7, 30s at SF10).

### REQ-LORA-008 — FOTA Window Protocol
The system shall implement windowed FOTA chunk delivery with 4–16 chunks in flight, 2s retry timer, and 3 retries per chunk before aborting.

### REQ-LORA-009 — Session Persistence
The system shall persist paired node sessions immediately after successful pairing via `settings_save()`. Sessions shall survive gateway reboot without re-pairing.

### REQ-LORA-010 — Ed25519 Key Caching
The system shall import the gateway's Ed25519 private key once at init and cache as `psa_key_id_t`, not import/destroy per beacon.

### REQ-LORA-011 — Duty Cycle Enforcement
The system shall enforce 1% EU868 duty cycle on a sliding window:
- At 0.9% utilization: defer change-triggered TX.
- At 1.0% utilization: skip all TX and buffer readings.

### REQ-LORA-012 — Change-Threshold Gating
The system shall gate transmissions on measurement change thresholds to avoid unnecessary TX for unchanged readings.

### REQ-LORA-013 — Keep-Alive Timer
The system shall implement a keep-alive timer per node to detect silent-node syndrome and trigger re-pairing if needed.

### REQ-LORA-014 — External Flash Buffering
The system shall use external SPI flash (W25Q64, 8 MB) to buffer sensor-node firmware images for FOTA over LoRa.

### REQ-LORA-015 — zbus-Only Integration
The system shall communicate with the rest of the gateway exclusively via zbus channels — no direct function calls to/from lora_radio.

## Dependencies
- REQ-FOTA-001: MCUboot dual-image slot management
- REQ-FOTA-004: HTTP transport for FOTA image upload
- REQ-SENSORS-005: Sensor UID identity for node addressing

## Constraints
- Frame overhead: 20 bytes (8B L2 header + 12B GCM tag)
- Payload budget: 31B at SF10/BW125, up to 235B at SF7/BW500
- Max readings per frame at SF10/BW125: 6 readings
- Minimum transmit interval at SF10/BW125: ~37 seconds (1% EU868)
- FOTA time for 128 KB: ~52 min at SF7/BW500, ~6.4 hr at SF7/BW125, ~33 hr at SF10/BW125
- Session key transmitted in plaintext during 30s pairing window
- Max paired nodes: CONFIG_LORA_RADIO_SESSION_MAX=16 (default)
- Default SF: 10, Default BW: 125 kHz

## Acceptance Criteria
- [ ] [native_sim] ztest: L2 header encode/decode round-trip
- [ ] [native_sim] ztest: AES-128-GCM encrypt/decrypt round-trip
- [ ] [native_sim] Integration: RPC retry logic delivers command within 3 retries
- [ ] [native_sim] Integration: FOTA window protocol delivers 16 chunks with ACK
- [ ] [native_sim] Integration: Session persists across gateway reboot
- [ ] [native_sim] Integration: Duty cycle enforcement defers TX at 1.0%
- [ ] [renode] Integration: End-to-end LoRa packet exchange between sensor node and gateway
- [ ] [hil] Board: Button-press pairing succeeds within 30s window
- [ ] [hil] Board: Ed25519 key cached at init, not re-imported per beacon
- [ ] [hil] Board: FOTA over LoRa delivers 128 KB image to sensor node

## Related ADRs
- ADR-015: LoRa Protocol Architecture
- ADR-006: LoRa Module Channel Boundary (superseded by ADR-015)
- ADR-002: zbus as System-Wide Communication Fabric

## Related Zephyr subsystems
- CONFIG_LORA
- CONFIG_CRYPTO
- CONFIG_PSA_CRYPTO
- CONFIG_SETTINGS
- CONFIG_FLASH
- CONFIG_SPI
