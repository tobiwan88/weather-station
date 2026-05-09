# ADR-015 — LoRa Protocol Architecture

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-05-09 |
| **Deciders** | Project founder |
| **Supersedes** | [ADR-006](ADR-006-lora-channel-boundary.md) |

---

## Context

ADR-006 (Deferred) defined the bounded-context boundary for `lib/lora_radio/`
but deferred all protocol-level decisions. With the full LoRa feature set now
designed — sensor data, FOTA relay, RPC, discovery/provisioning — a
comprehensive protocol ADR is needed to define the frame format, security model,
and integration pattern with the existing zbus architecture.

ADR-014 (MCUboot FOTA) defines a `fota_relay_transport_api` stub for
sensor-node firmware relay. The LoRa FOTA design fulfills this role via
`lora_fota_chan` (zbus), consistent with ADR-002's no-direct-calls rule.

### Physical topology

The gateway (`frdm_mcxn947`) has no built-in LoRa radio. LoRa communication
requires a dedicated co-processor:

```
┌──────────────────────┐   UART    ┌────────────────────────────┐
│  frdm_mcxn947        │◄─────────►│  STM32WLE5JC (LoRa E5-Mini)│
│  Gateway app         │           │                            │
│  All consumer libs   │           │  lib/lora_radio/           │
│  http_dashboard      │           │    Radio driver (SX1262)   │
│  mqtt_publisher      │           │    Packet framing + GCM    │
│  sensor_registry     │           │    Session manager         │
│  remote_sensor_mgr   │           │    Protocol handlers       │
│  lora_proxy (future) │           │  zbus channels (local)     │
└──────────────────────┘           └────────────────────────────┘
```

The STM32WLE5JC runs `lib/lora_radio/` with local zbus channels. The gateway
will bridge these channels using the zbus **Proxy Agent** feature (experimental
in Zephyr 4.4) — shadow channels on the gateway mirror the STM32WLE5JC's local
channels, and a proxy agent (UART transport backend) synchronizes messages
between the two MCUs. The proxy/forwarding design is **deferred** to a subsequent
ADR; the protocol and library design in this ADR focus on the LoRa layer itself.

Requirements derived from project goals:

1. **Point-to-point star** — one gateway, up to 256 sensor nodes (no mesh, no LoRaWAN).
2. **Compact sensor data** — minimal per-reading overhead for duty-cycle efficiency.
3. **Reliable FOTA and RPC** — ACK-based delivery for firmware updates and commands.
4. **Secure** — AES-128-GCM per-frame encryption + Ed25519 gateway authentication during pairing.
5. **Discoverable** — button-press pairing, not pre-provisioned UIDs.
6. **zbus-native** — no vtable dispatch between manager and transports; all runtime communication through zbus channels.

---

## Decision

### Frame format

A fixed 8-byte L2 header shared across all frame types, followed by a
type-specific payload, with a 12-byte AES-128-GCM authentication tag appended
to every frame after pairing.

```
Byte 0:    type (4b) + protocol_version (4b)
Byte 1:    flags (ACK_REQ | ENCRYPTED | FRAGMENTED | reserved)
Bytes 2–3: src_node (uint16 LE)
Bytes 4–5: dst_node (uint16 LE)
Bytes 6–7: seq_num (uint16 LE)
Bytes 8+:  frame-type-specific payload
Last 12B:  GCM authentication tag
```

Total overhead: 20 bytes. Payload budget: 31 bytes at SF12/BW125 (~51B max)
up to 235 bytes at SF7/BW500 (255B max).

### Frame types (4 bits = 16 slots)

| ID | Name | Reliable | Purpose |
|----|------|----------|---------|
| 0x0 | SENSOR_DATA | fire & forget | N × 5B readings (type + Q31) |
| 0x1 | SENSOR_DATA_ACK | optional | last_seq acknowledgment |
| 0x2 | RPC_CMD | ACK + retry | cmd_id + params |
| 0x3 | RPC_RESP | — | cmd_id + status + data |
| 0x4 | FOTA_CHUNK | ACK + retry | offset + chunk data |
| 0x5 | FOTA_CHUNK_ACK | — | offset + status |
| 0x6 | PROV_BEACON | periodic | capability TLVs + pubkey + nonce |
| 0x7 | PROV_RESPONSE | — | node_id + session_key + sig |
| 0x8 | ACK/NACK | — | standalone acknowledgment |
| 0x9–0xF | RESERVED | — | future expansion |

### Reliability model

- **Sensor data**: best-effort, no ACK. Packet loss means a missed reading
  (acceptable for environmental data).
- **RPC commands**: ACK_REQ flag set. Up to 3 retries, SF-dependent timeout
  (5s at SF7, 30s at SF10). One pending RPC per node (serialized).
- **FOTA chunks**: windowed ACK protocol. 4–16 chunks in flight, 2s retry
  timer, 3 retries per chunk. ACK carries written offset + status.

### Security

- **AES-128-GCM** encrypts payload and authenticates header + payload on every
  post-pairing frame. 12-byte GCM tag appended.
- **Ed25519** gateway signature on PROV_RESPONSE during pairing. Sensor node
  verifies before accepting session key.
- **Sequence numbers** prevent replay within a session.
- **Session keys** provisioned during pairing, persisted in Zephyr Settings
  (gateway) and NVS (sensor node). Survive reboot.
- **Key exchange during pairing is plaintext** — relies on short 30s pairing
  window, low TX power, and physical proximity. Acceptable for home deployment
  (matching ADR-014's security posture).

### Provisioning

Button-press pairing with capability advertisement:

1. User presses button on sensor node → 30s pairing mode.
2. Node broadcasts PROV_BEACON (capability TLVs + Ed25519 public key + nonce).
3. Gateway receives, assigns node_id from pool (0x0001–0x00FF), generates
   AES-128 session key.
4. Gateway sends PROV_RESPONSE (node_id + session_key) signed with gateway's
   Ed25519 key. Frame is 134 bytes with overhead — requires SF7/BW125 or higher.
5. Node verifies signature, stores session key in NVS, enters normal operation.
6. Gateway publishes `remote_discovery_event` FOUND for each sensor type →
   sensor_registry auto-registers.

After reboot: sessions restored from persistent storage. No re-pairing needed.

### RPC command set

Commands grouped by function, IDs reserved per group:

| Range | Group | Commands |
|-------|-------|----------|
| 0x00–0x0F | Diagnostics | PING, GET_VERSION, GET_STATUS |
| 0x10–0x1F | Config | SET_PUBLISH_INTERVAL, SET_SPREADING, SET_TX_POWER, SET_KEEPALIVE, SET_CHANGE_THRESHOLD |
| 0x20–0x2F | Trigger | TRIGGER_SAMPLE |
| 0x30–0x3F | FOTA | FOTA_START (supports full-image and cpatch delta modes), FOTA_CANCEL |
| 0xF0–0xFF | System | REBOOT |

### Compact sensor data encoding

Each reading on the wire: 5 bytes (1B `sensor_type` + 4B `q31_value`).
No timestamp (gateway assigns `k_uptime_get()` on receipt).
No UID (derived from L2 header `src_node` + `sensor_type`).

This is a 4× reduction from the full 20-byte `env_sensor_data` struct.

At SF10/BW125 (default data rate): 6 readings per frame, ~37s minimum interval,
0.48% duty cycle for a temp+hum node at 60s interval.

### Integration with existing architecture

`lib/lora_radio/` is a bounded context implementing the `remote_transport`
iterable section pattern. **No vtable dispatch** — all runtime communication
between the remote_sensor_manager and lora_radio uses zbus channels:

| Channel | Direction | Purpose |
|---------|-----------|---------|
| `sensor_event_chan` | LoRa → consumers | Decoded env_sensor_data (existing) |
| `sensor_trigger_chan` | Trigger sources → LoRa | Forward trigger to sensor node (existing) |
| `remote_scan_ctrl_chan` | Manager → LoRa | Scan start/stop (existing) |
| `remote_discovery_chan` | LoRa → Manager | Discovery found/lost (existing) |
| `remote_peer_cmd_chan` | Manager → LoRa | Peer add/remove/send_trigger (new) |
| `lora_link_chan` | LoRa → diagnostics | RSSI, SNR, seq_num (new) |
| `lora_fota_chan` | HTTP dashboard → LoRa | FOTA start/chunk/cancel (new) |

Transports auto-register at compile time via `REMOTE_TRANSPORT_REGISTER()` in
the iterable section (name, proto, caps only — no function pointers). The
manager reads this section at boot for bookkeeping. After init, all interaction
is through zbus.

### FOTA over LoRa

- Gateway stores sensor-node firmware image on external W25Q64 SPI flash.
- `POST /api/fota/sensor/<uid>/apply` → `http_dashboard` publishes FOTA_START
  on `lora_fota_chan`.
- `lora_radio` subscriber reads from SPI flash, chunks image, sends via
  FOTA_CHUNK frames with windowed ACK.
- Sensor node writes to MCUboot slot 1, verifies ED25519 signature,
  `boot_request_upgrade(TEST)`, reboots.
- Supports `fota_mode = 0x01` for cpatch delta updates (future).
- ~52 minutes for 128 KB image at SF7/BW500; ~6.4 hours at SF7/BW125.

---

## Rationale

### Why fixed header over TLV

LoRa payloads are tiny (51–255 bytes). A variable-length TLV header would
consume 2+ bytes per field, quickly eating into the sensor data budget. The
fixed 8-byte header is predictable, trivially parseable on Cortex-M4, and leaves
maximum room for payload. Extensibility comes from the 4-bit type field (16
slots, 6 reserved) and the flags byte (5 reserved bits).

### Why AES-128-GCM over plaintext

SDRs capable of receiving LoRa are inexpensive (~$30 RTL-SDR). Without
encryption, sensor data and commands are readable by anyone in range. AES-128-GCM
provides both confidentiality and authentication with a single algorithm, at
~12 bytes overhead per frame. The Cortex-M4 on STM32WLE5JC has hardware AES
acceleration, making per-frame decryption fast.

### Why no vtable dispatch

ADR-002 requires all inter-library coordination through zbus channels. The
original `remote_transport` vtable (ADR-006's approach) creates direct function
calls between `remote_sensor_manager` and transports — a violation of this rule.
The revised design keeps the iterable section for compile-time registration
(metadata only) and moves all runtime operations to zbus channels. Each
transport subscribes independently; the manager never calls a transport directly.

### Why button-press pairing over pre-provisioned UIDs

ADR-006 specified "gateway pre-knows remote sensor UIDs." This is inflexible:
adding a new sensor node requires recompiling the gateway's devicetree. Button-
press pairing allows runtime discovery with intentional user action. The 30s
window and low TX power limit the attack surface. Pre-provisioning remains
available as a Kconfig option for deployments that prefer it.

### Why change-threshold gating

Without it, a sensor node transmits at fixed intervals regardless of whether
anything changed — wasting duty cycle and power. With per-sensor-type change
thresholds (e.g., 0.5°C for temperature), the node only transmits when data is
meaningfully different. The periodic publish interval provides a fallback to
prevent silent-node syndrome, and the separate keep-alive interval covers nodes
where all types are below thresholds for extended periods.

---

## Consequences

### Positive

- Compact 5-byte sensor readings, 4× smaller than full `env_sensor_data`.
- Single protocol covers data, FOTA, RPC, and provisioning — one parser.
- zbus-only integration preserves the "no library-to-library calls" rule.
- Button-press pairing enables runtime node addition.
- AES-128-GCM protects all frames after pairing.
- Extensible: 6 reserved frame types, 5 reserved flag bits, open command ID space.
- Separation of concerns: LoRa protocol runs on dedicated STM32WLE5JC co-processor;
  gateway remains transport-agnostic.

### Negative / Constraints

- **Duty cycle limits FOTA speed**: a 128 KB image takes ~52 minutes at best
  (SF7/BW500). Operator must plan updates accordingly.
- **No Diffie-Hellman key exchange**: session key is transmitted in plaintext
  during the 30s pairing window. Relies on physical proximity. Acceptable for
  home deployment; not suitable for public environments without DH upgrade.
- **Gateway must have external SPI flash** (W25Q64) to buffer sensor-node firmware
  images before LoRa relay.
- **Sequence number wrap**: 16-bit seq_num wraps after 65,535 frames. At 60s
  intervals, this is ~45 days. Sessions should be re-keyed before wrap.
- **SF negotiation is implicit**: FOTA sessions use highest mutually-supported SF
  determined during pairing; no dynamic rate adaptation mid-session.
- **UART proxy is deferred**: the zbus proxy agent bridging gateway ↔ STM32WLE5JC
  over UART is not designed here. All zbus channel contracts are defined, but the
  forwarding transport (UART backend + shadow channels) will be addressed in a
  separate ADR. Until then, `lib/lora_radio/` assumes local zbus channels on the
  STM32WLE5JC.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| LoRaWAN (TTN/ChirpStack) | Requires gateway hardware + network server; adds complexity for a home LAN system (same as ADR-006) |
| CBOR/Protobuf encoding for data | Adds parsing complexity and code size; 5-byte fixed encoding is simpler and smaller |
| Full vtable dispatch (ADR-006) | Direct function calls violate ADR-002. zbus channels provide the same decoupling with better composability |
| Pre-shared keys only (no pairing protocol) | Inflexible; every node addition requires recompiling firmware |
| DH key exchange during pairing | Adds significant code complexity (Curve25519) for marginal security gain in a home deployment. Can be added later |
| Variable-length TLV header | Too much overhead for 51-byte frames. Fixed header is adequate for a purpose-built protocol |

---

## Related

- [ADR-002](ADR-002-zbus-as-system-bus.md) — zbus as system bus (zbus-only integration)
- [ADR-006](ADR-006-lora-channel-boundary.md) — LoRa bounded context (superseded)
- [ADR-014](ADR-014-mcuboot-fota.md) — MCUboot FOTA (fota_relay_transport_api)
- [ADR-008](ADR-008-kconfig-app-composition.md) — Kconfig composition
- [`docs/architecture/lora-protocol.md`](../architecture/lora-protocol.md) — detailed architecture
