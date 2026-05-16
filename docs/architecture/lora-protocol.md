# LoRa Protocol Architecture

This document covers the detailed architecture for the LoRa wireless communication subsystem.

> Design rationale: [ADR-006](../adr/ADR-006-lora-channel-boundary.md), [ADR-015](../adr/ADR-015-lora-protocol.md).

---

## Overview

`lib/lora_radio/` is the LoRa bounded context — a Kconfig-gated, self-wiring
Zephyr library that handles all LoRa-specific concerns (radio driver, packet
framing, session management, protocol dispatch). It runs on the **STM32WLE5JC
LoRa co-processor** (LoRa E5-Mini), not on the gateway itself.

### Physical topology

The gateway (`frdm_mcxn947`) has no built-in LoRa radio. LoRa communication
requires a dedicated co-processor connected via UART:

```
┌──────────────────────────┐   UART    ┌──────────────────────────────┐
│  frdm_mcxn947 (gateway)  │◄─────────►│  STM32WLE5JC (LoRa module)   │
│                          │           │                              │
│  All consumer libs:      │           │  apps/lora_bridge/:          │
│   sensor_event_log       │           │    lib/lora_radio/           │
│   http_dashboard         │           │      Radio driver (SX1262)   │
│   lvgl_display           │           │      Packet framing + GCM    │
│   mqtt_publisher         │           │      Session manager         │
│                          │           │      Protocol handlers       │
│  lib/uart_lora_bridge/   │           │    lib/uart_lora_sender/     │
│   (gateway side)         │  24-byte  │      zbus → UART TX bridge   │
│   UART → zbus            │  wire     │                              │
│                          │  frames   │  local zbus channels:        │
│  remote_sensor_manager   │           │    sensor_event_chan         │
│  sensor_registry         │           │    lora_link_chan            │
│                          │           │    remote_discovery_chan     │
│                          │           │    remote_scan_ctrl_chan     │
│                          │           │    remote_peer_cmd_chan      │
│                          │           │    lora_fota_chan             │
└──────────────────────────┘           └──────────────────────────────┘
```

The current implementation uses a **simple 24-byte wire frame protocol**
(magic `0x5A 0xA5`, length, 20-byte packed payload, CRC8) instead of the
zbus Proxy Agent. On the gateway side, `lib/uart_lora_bridge/` receives frames
over UART and publishes `env_sensor_data` to `sensor_event_chan`. On the
co-processor side, `lib/uart_lora_sender/` subscribes to `sensor_event_chan`
and sends frames over UART. The zbus Proxy Agent design is **deferred** — see
ADR-015 for the future proxy/forwarding design.

### Library integration (post-proxy, on-gateway perspective)

Once the UART proxy is implemented, the gateway sees `lib/lora_radio/`'s
channels as local shadow channels — consumers (dashboard, MQTT, display)
subscribe to them identically to local sensor channels:

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CONSUMERS (unchanged)                         │
│  sensor_event_log  │  http_dashboard  │  lvgl_display  │  mqtt_pub  │
└──────────────────────────┬──────────────────────────────────────────┘
                           │  subscribe
                    sensor_event_chan
              ┌────────────┼────────────┐
              │            │            │
        fake_sensors        │      pipe_transport
        (local)             │      (native_sim test)
                           │
            ┌──────────────┴──────────────┐
            │   lib/remote_sensor/        │
            │   (transport-agnostic mgr)  │
            │   zbus-based dispatch       │
            │   discovery → sensor_reg    │
            └──────────────┬──────────────┘
                           │  zbus channels
            ┌──────────────┼──────────────┐
            │              │              │
      fake_remote     pipe_transport    ★ lora_radio ★
      (test stub)     (FIFO test)       (this design)

┌─────────────────────────────────────────────────────────────────────┐
│               ★ lib/lora_radio/ — Bounded Context ★                  │
│                                                                     │
│  ┌──────────┐   ┌───────────┐   ┌───────────┐   ┌──────────────┐  │
│  │  Radio   │   │  Packet   │   │  Session  │   │   Protocol   │  │
│  │  Driver  │   │  Framing  │   │  Manager  │   │   Handlers   │  │
│  │          │   │           │   │           │   │              │  │
│  │ SX1276/  │   │ 8B hdr +  │   │ pairing   │   │ data_handler │  │
│  │ SX1262   │   │ GCM tag   │   │ state     │   │ rpc_handler  │  │
│  │ SPI      │   │ CRC16     │   │ keys      │   │ fota_handler │  │
│  │ IRQ      │   │           │   │ node_map  │   │ prov_handler │  │
│  └──────────┘   └───────────┘   └───────────┘   └──────────────┘  │
│                                                                     │
│  EXTERNAL SURFACE:                                                  │
│    → publishes env_sensor_data × N  →  sensor_event_chan            │
│    → publishes lora_link_info       →  lora_link_chan (diagnostics) │
│    → subscribes to remote_scan_ctrl_chan, remote_peer_cmd_chan      │
│    → subscribes to sensor_trigger_chan, lora_fota_chan              │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Frame Protocol

### L2 Header (8 bytes, shared across all frame types)

```
Byte 0:    [type:4][version:4]
Byte 1:    [flags:8]
Bytes 2–3: src_node (uint16 LE)
Bytes 4–5: dst_node (uint16 LE)
Bytes 6–7: seq_num (uint16 LE)
```

### Flags byte

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | ACK_REQ | Receiver must send ACK/NACK |
| 1 | ENCRYPTED | Payload + GCM tag present (always 1 after pairing) |
| 2 | FRAGMENTED | Payload spans multiple frames |
| 3–7 | RESERVED | Must be 0 |

### Frame types

| ID | Name | Reliable | Payload structure |
|----|------|----------|-------------------|
| 0x0 | SENSOR_DATA | fire & forget | N × (1B sensor_type + 4B Q31) |
| 0x1 | SENSOR_DATA_ACK | optional | last_seq (2B LE) |
| 0x2 | RPC_CMD | ACK + retry | cmd_id (1B) + param_len (1B) + params |
| 0x3 | RPC_RESP | — | cmd_id (1B) + status (1B) + data |
| 0x4 | FOTA_CHUNK | ACK + retry | chunk_offset (4B LE) + chunk_data |
| 0x5 | FOTA_CHUNK_ACK | — | chunk_offset (4B LE) + status (1B) |
| 0x6 | PROV_BEACON | periodic | caps_count (1B) + capability_TLVs + pubkey (32B) + nonce (16B) + fw_ver |
| 0x7 | PROV_RESPONSE | — | node_id (2B) + session_key (16B) + gateway_pubkey (32B) + sig (64B) |
| 0x8 | ACK/NACK | — | ack_seq (2B LE) + status (1B) |
| 0x9–0xF | RESERVED | — | future expansion |

### Frame budget by spreading factor

| SF / BW | Max frame | Overhead | Payload | Readings (5B each) | Min interval (1% duty) |
|---------|-----------|----------|---------|---------------------|------------------------|
| SF12/BW125 | 51 B | 20 B | 31 B | 6 | 99 s |
| SF11/BW125 | 51 B | 20 B | 31 B | 6 | 58 s |
| SF10/BW125 | 51 B | 20 B | 31 B | 6 | 37 s |
| SF9/BW125 | 115 B | 20 B | 95 B | 19 | 18.5 s |
| SF8/BW125 | 222 B | 20 B | 202 B | 40 | 10.3 s |
| SF7/BW125 | 222 B | 20 B | 202 B | 40 | 6.2 s |
| SF7/BW500 | 255 B | 20 B | 235 B | 47 | 5.6 s |

Default for sensor data: SF10/BW125. For FOTA: SF7/BW500.

---

## Internal Modules

### Radio Driver (`lora_radio_drv.c`)

SPI-based HAL for SX1276/SX1262. Provides:

- TX/RX FIFO management
- Channel activity detection (CAD)
- RSSI / SNR readback
- Interrupt-driven RX via GPIO
- Configurable SF / BW via Kconfig

Abstracted behind a `struct lora_radio_ops` to allow a fake radio backend
(`CONFIG_LORA_RADIO_FAKE`) for native_sim testing.

### Packet Framing (`lora_packet.c`)

Pure functions, no global state:

- `lora_packet_encode()` — builds L2 header + payload buffer
- `lora_packet_decode()` — validates and parses L2 header
- `lora_packet_encrypt()` — AES-128-GCM encrypt + tag
- `lora_packet_decrypt()` — AES-128-GCM decrypt + verify
- `lora_packet_crc16()` — appends/validates radio-level CRC

CRC16 is frame-level (radio hardware typically handles this, but the function
is available for the fake radio backend).

### Session Manager (`lora_session.c`)

Maintains per-node state:

- `node_id` → `session_key` mapping
- `last_seq_rx` / `last_seq_tx` (sequence number tracking)
- `retry_count` and `last_ack_time` for reliable frames
- `pairing_state` enum: UNPAIRED, PAIRING, PAIRED, EXPIRED
- Node ID allocation pool (0x0001–0x00FF for LoRa)
- Persistence to Zephyr Settings (`lora/<node_id>/key`)

On reboot: restores all sessions from Settings. Invalidates sessions where
GCM auth fails (the node was unpaired or key rotated).

### Protocol Handlers (`lora_handler_*.c`)

Dispatch table indexed by frame type byte. Each handler:

1. Validates GCM tag (if ENCRYPTED flag set)
2. Checks sequence number against session state (if ACK_REQ or reliable)
3. Dispatches to the correct path:
   - **data_handler**: unpacks N × 5B readings, calls `remote_sensor_publish_data()`
   - **rpc_handler**: dispatches to command table, builds RPC_RESP
   - **fota_handler**: writes chunk to `flash_img`, sends FOTA_CHUNK_ACK
   - **prov_handler**: validates beacon, assigns node_id + key, sends PROV_RESPONSE
4. For reliable frames: schedules ACK transmission, updates session state

Runs on the dedicated LoRa RX thread.

---

## Zbus Channels

### Existing channels (no changes)

| Channel | Owner | LoRa role |
|---------|-------|-----------|
| `sensor_event_chan` | `sensor_event` | Publishes decoded `env_sensor_data` via `remote_sensor_publish_data()` |
| `sensor_trigger_chan` | `sensor_trigger` | Subscribes: when `target_uid` matches a paired LoRa node, sends TRIGGER_SAMPLE RPC |
| `remote_scan_ctrl_chan` | `remote_sensor` | Subscribes: on SCAN_START for LORA proto, enables RX continuous mode |
| `remote_discovery_chan` | `remote_sensor` | Publishes: FOUND/LOST events during pairing and session expiry |
| `config_cmd_chan` | `config_cmd` | Subscribes: new LORA_RPC command type → `lora_radio_rpc_send()` |

### New channels

| Channel | Owner | Direction | Message |
|---------|-------|-----------|---------|
| `remote_peer_cmd_chan` | `remote_sensor` | Manager → Transports | `remote_peer_cmd_event` (PEER_ADD, PEER_REMOVE, SEND_TRIGGER) |
| `lora_link_chan` | `lora_radio` | LoRa → Diagnostics | `lora_link_event` (RSSI, SNR, node_id, seq_num) |
| `lora_fota_chan` | `lora_radio` | HTTP → LoRa | `lora_fota_event` (FOTA_START, FOTA_CANCEL, image_size, chunk data) |

### remote_peer_cmd_event

```c
struct remote_peer_cmd_event {
    enum { PEER_ADD, PEER_REMOVE, SEND_TRIGGER } action;
    enum remote_transport_proto proto;
    uint32_t target_uid;
    uint8_t  peer_addr[REMOTE_SENSOR_ADDR_MAX_LEN];
    uint8_t  addr_len;
};
```

Published by `remote_sensor_manager` after a sensor is registered/deregistered
in `sensor_registry`, or when a trigger targets a remote sensor. Each transport
subscribes and checks `proto` to filter.

### lora_link_event

```c
struct lora_link_event {
    uint16_t node_id;
    int16_t  rssi;      // dBm
    uint8_t  snr;       // dB
    uint16_t seq_num;
    uint16_t crc_errors; // since last report
};
```

Published after each received frame. Initially consumed only by
`sensor_event_log` (debug output). Future: dashboard widget showing per-node
link quality.

### lora_fota_event

```c
struct lora_fota_event {
    enum { FOTA_START, FOTA_CANCEL } action;
    uint32_t target_uid;
    uint32_t image_size;
    uint8_t  fota_mode;   // 0x00 = full image, 0x01 = cpatch delta
};
```

Published by `http_dashboard` for sensor-node FOTA. `lora_radio` subscribes,
reads the image from external SPI flash, and chunks it over LoRa. The FOTA
chunk data itself does not traverse this channel — it's read directly from
flash by the lora_radio FOTA handler.

---

## Transport Registration

Transports auto-register at compile time via an iterable section. Unlike the
original vtable design, the section carries only metadata — no function
pointers:

```c
/* In lib/lora_radio/src/main.c */
REMOTE_TRANSPORT_REGISTER(lora_transport, {
    .name  = "lora",
    .proto = REMOTE_TRANSPORT_PROTO_LORA,
    .caps  = REMOTE_TRANSPORT_CAP_SCAN,
});
```

The `remote_sensor_manager` reads this section at boot to know which transports
exist, creates per-proto bookkeeping, and subscribes to `remote_discovery_chan`.
All runtime interaction — scan control, peer management, trigger forwarding —
goes through zbus channels. The manager never calls a transport function directly.

---

## Provisioning Flow

```
Sensor Node (STM32WLE5JC)               Gateway (frdm_mcxn947)
│                                       │
│  [User presses button]                │
│  Enter pairing mode (30s timer)       │
│                                       │
│  PROV_BEACON (frame 0x6) ───────────►│
│  {caps: TEMP+HUM,                    │  Validate capabilities
│   pubkey: Ed25519_pk,                │  Check no duplicate pubkey
│   nonce: rand[16],                   │  Assign node_id from pool
│   fw_ver: "1.0.0"}                   │  Generate AES-128 session_key
│                                       │
│                      ◄─────────────── PROV_RESPONSE (frame 0x7)
│                      {node_id, session_key, gateway_pubkey, sig}
│                                       │
│  Verify Ed25519 signature             │
│  Store session_key in NVS             │
│  Exit pairing mode                    │
│                                       │
│  SENSOR_DATA (frame 0x0) ───────────►│
│  (encrypted, first reading)           │  Decrypt, validate GCM
│                                       │  remote_sensor_publish_data()
│                                       │  Persist session in Settings
│                                       │
│  [After reboot: session_key in NVS]   │  [After reboot: sessions
│   Resume normal operation             │   restored from Settings]
```

### Security properties

| Property | Mechanism |
|----------|-----------|
| Gateway authentication | PROV_RESPONSE signed with Ed25519 |
| Session key confidentiality | Plaintext in PROV_RESPONSE — relies on 30s window + proximity |
| Replay protection | Nonce in beacon; sequence numbers in data frames |
| Frame encryption | AES-128-GCM on all post-pairing frames |
| Unpair | Gateway sends PEER_REMOVE; node wipes key; GCM auth fails → self-reset to pairing mode |

---

## Compact Sensor Data

### Wire format

Each reading on the wire is **5 bytes**:

```
Byte 0:    sensor_type (uint8)
Bytes 1–4: q31_value (int32 LE)
```

No timestamp (gateway assigns `k_uptime_get()` on receipt).
No UID (derived from L2 header `src_node` + `sensor_type`).

### Gateway reconstruction

```c
void lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t len)
{
    uint8_t num_readings = len / 5;

    for (uint8_t i = 0; i < num_readings; i++) {
        enum sensor_type type = (enum sensor_type)payload[i * 5 + 0];
        int32_t q31;
        memcpy(&q31, &payload[i * 5 + 1], sizeof(q31));

        uint32_t uid = lora_uid_for_reading(src_node, type);
        remote_sensor_publish_data(uid, type, q31);
    }
}
```

### Sensor node TX decision logic

```
On each sample cycle (k_timer at publish_interval):
  for each sensor_type:
    reading = sensor_read(type)

    if change_threshold[type] > 0:
      delta = abs(reading - last_sent[type])
      if delta >= change_threshold[type]:
        enqueue_for_tx(type, reading)
        last_sent[type] = reading
        continue

    if time_since_last_periodic >= publish_interval:
      enqueue_for_tx(type, reading)
      last_sent[type] = reading

  if enqueued_readings > 0:
    pack into SENSOR_DATA frame(s)
    TX respecting duty cycle limit

Separate keep-alive timer:
  if keepalive_interval > 0 AND
     (now - last_any_tx) >= keepalive_interval:
    TX minimal keep-alive frame

Duty cycle enforcement (1% sliding window):
  at 0.9%: defer change-triggered TX, keep periodic
  at 1.0%: skip all TX, buffer readings
```

---

## RPC Command Set

### Frame formats

**RPC_CMD (frame type 0x2):**
```
Byte 0:    cmd_id (uint8)
Byte 1:    param_len (uint8)
Bytes 2+:  params (command-specific)
```
Flags: ACK_REQ=1, ENCRYPTED=1

**RPC_RESP (frame type 0x3):**
```
Byte 0:    cmd_id (uint8, echoed)
Byte 1:    status (uint8, 0x00 = OK)
Bytes 2+:  response data
```

### Command table

| ID | Command | Params | Response |
|----|---------|--------|----------|
| 0x00 | PING | — | uptime_ms (4B) |
| 0x01 | GET_VERSION | — | fw_ver (null-term string) |
| 0x02 | GET_STATUS | — | battery_mv (2B), rssi (1B), uptime (4B) |
| 0x10 | SET_PUBLISH_INTERVAL | interval_s (2B) | status |
| 0x11 | SET_SPREADING | sf (1B), bw (1B) | status |
| 0x12 | SET_TX_POWER | dBm (1B, signed) | status |
| 0x13 | SET_KEEPALIVE | interval_s (2B) | status |
| 0x14 | SET_CHANGE_THRESHOLD | sensor_type (1B), q31_delta (4B) | status |
| 0x20 | TRIGGER_SAMPLE | sensor_type (1B, 0xFF=all) | num_samples (1B) |
| 0x30 | FOTA_START | image_size (4B), fota_mode (1B) | status |
| 0x31 | FOTA_CANCEL | — | status |
| 0xFF | REBOOT | delay_ms (2B) | status |

### Reliability

- One pending RPC per node (serialized)
- SF-dependent timeout: 5s at SF7, 30s at SF10
- Up to 3 retries per command
- Duplicate detection: sensor node checks seq_num, re-sends last response
- RPC_RESP serves as implicit ACK — no separate ACK frame

---

## FOTA over LoRa

### Flow

```
Browser → POST /api/fota/sensor/<uid>/upload → Gateway stores in SPI flash
Browser → POST /api/fota/sensor/<uid>/apply  → http_dashboard publishes FOTA_START
                                               → lora_radio starts chunk loop

Gateway (lora_radio)                       Sensor Node (STM32WLE5JC)
│                                          │
│  Read chunk from SPI flash               │
│  FOTA_CHUNK (offset=N, data) ──────────►│
│                                          │  flash_img_buffered_write(slot1, data)
│                       ◄────────────────── FOTA_CHUNK_ACK (offset=N, OK)
│                                          │
│  ... repeat for all chunks ...           │
│                                          │
│  FOTA_CHUNK (offset=final, LAST flag) ─►│
│                                          │  Verify ED25519 signature
│                                          │  boot_request_upgrade(TEST)
│                                          │  Reboot
│                                          │
│                                          │  [MCUboot: verify, swap, boot new]
│                                          │
│                       ◄────────────────── SENSOR_DATA (new fw version in caps)
│                                          │
│  Publish FOTA_COMPLETE                   │
```

### Chunk protocol

- Window size: 4–16 chunks in flight (Kconfig)
- Retry timer: 2 seconds per chunk
- Max retries: 3 per chunk
- Consecutive timeout cooldown: 30 seconds pause after 3 failures
- Flash error: abort session immediately
- ACK carries written offset → gateway skips completed chunks on retry

### FOTA modes

| Mode | Value | Description |
|------|-------|-------------|
| Full image | 0x00 | Write complete image to MCUboot slot 1 |
| cpatch delta | 0x01 | Apply binary patch to existing slot 1 image (future) |

### Duty cycle budget

| SF / BW | Chunk size | Airtime | Interval | 128 KB image |
|---------|------------|---------|----------|-------------|
| SF7/BW500 | 231 B | ~56 ms | 5.6 s | ~52 min |
| SF7/BW125 | 50 B | ~72 ms | 7.2 s | ~6.4 hr |
| SF10/BW125 | 50 B | ~371 ms | 37.1 s | ~33 hr |

Gateway auto-negotiates to highest supported SF during FOTA_START.

---

## Kconfig

```
CONFIG_LORA_RADIO                  — Master switch
CONFIG_LORA_RADIO_DRV_SX1276       — Semtech SX1276 driver (choice)
CONFIG_LORA_RADIO_DRV_SX1262       — Semtech SX1262 driver (choice)
CONFIG_LORA_RADIO_FAKE             — Fake radio for native_sim testing
CONFIG_LORA_RADIO_FOTA             — Enable FOTA chunk handler
CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE  — Max chunk data size (default 231)
CONFIG_LORA_RADIO_FOTA_WINDOW      — In-flight chunks (default 8)
CONFIG_LORA_RADIO_AUTO_PUBLISH_MS  — Periodic trigger forwarding (0 = disabled)
CONFIG_LORA_RADIO_RX_THREAD_STACK  — RX thread stack (default 2048)
CONFIG_LORA_SENSOR_PUBLISH_INTERVAL_S — Default publish interval (default 60)
CONFIG_LORA_SENSOR_KEEPALIVE_S     — Default keep-alive interval (default 0)
CONFIG_LORA_RADIO_DEFAULT_SF       — Default spreading factor (default 10)
CONFIG_LORA_RADIO_DEFAULT_BW       — Default bandwidth (default 125)
CONFIG_LORA_RADIO_SESSION_MAX      — Max paired nodes (default 16)
```

---

## Future Extensions

| Feature | How it fits |
|---------|-------------|
| **UART proxy agent** | `CONFIG_ZBUS_PROXY_AGENT` + UART transport backend bridges gateway ↔ STM32WLE5JC channels. Shadow channels on gateway mirror `sensor_event_chan`, `lora_link_chan`, `remote_discovery_chan`, etc. Gateway consumers see no difference. Deferred to next ADR. |
| cpatch delta FOTA | `fota_mode = 0x01` in FOTA_START — no protocol change |
| New sensor types | Add `sensor_type` enum value — consumers ignore unknown types |
| New RPC commands | Add command ID in reserved range — no framing change |
| DH key exchange | Add to PROV_BEACON/RESPONSE as optional TLV — backward compatible |
| Mesh / repeater | New frame type + dst_node forwarding logic — header already supports it |
| LoRaWAN | Replace `lib/lora_radio/` internals — external surface (same zbus channels) unchanged |

---

## Related

- [ADR-015](../adr/ADR-015-lora-protocol.md) — architectural decision record
- [ADR-006](../adr/ADR-006-lora-channel-boundary.md) — bounded context (superseded)
- [ADR-014](../adr/ADR-014-mcuboot-fota.md) — MCUboot FOTA
- [event-bus.md](event-bus.md) — zbus channel architecture
- [system-overview.md](system-overview.md) — system layers and library roles
