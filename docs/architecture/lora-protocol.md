# LoRa Protocol Architecture

This document covers the detailed architecture for the LoRa wireless communication subsystem.

> Design rationale: [decisions](../decisions.md).

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
│  │ SX1276/  │   │ 10B hdr + │   │ sessions  │   │ data_handler │  │
│  │ SX1262   │   │ GCM tag + │   │ PSK keys  │   │ rpc_handler  │  │
│  │ SPI      │   │ CRC16     │   │ replay    │   │ fota_handler │  │
│  │ IRQ      │   │           │   │ node_map  │   │              │  │
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

### L2 Header (10 bytes, shared across all frame types)

```
Byte 0:    [type:4][version:4]
Byte 1:    [flags:8]
Bytes 2–3: src_node (uint16 LE)
Bytes 4–5: dst_node (uint16 LE)
Bytes 6–9: seq_num (uint32 LE)
```

The header is the AAD of the GCM tag. The frame on the air is
`header | payload (ciphertext) | GCM tag (12 B) | CRC16 (2 B, LE)`; the CRC16
(CCITT, init 0xFFFF) covers everything before it, so the fixed overhead is
24 bytes.

### Nonce (AES-128-GCM, 12 bytes, built inside `lora_proto`)

```
Bytes 0–1:  node_id (uint16 LE) — the non-gateway end: src for uplink, dst for downlink
Byte  2:    direction (0 = uplink, 1 = downlink; mirrors LORA_FLAG_DIR_DOWN)
Byte  3:    0
Bytes 4–7:  seq_num (uint32 LE)
Bytes 8–11: 0
```

A (key, node, direction, seq) tuple must never repeat; callers only choose the
seq. The node persists its counter, the gateway keeps one counter per node.

Sequence and nonce rules:

- Every transmitted frame takes a fresh seq (`lora_session_next_tx_seq()` on the
  gateway). This includes ACKs, responses and retransmissions.
- A retransmission is a new frame: it is re-encoded with a new seq and therefore a
  new nonce. Reusing the seq would repeat the AES-GCM nonce and the receiver's
  replay protection (`seq <= last_seq_rx` is dropped) would discard the frame.
- Consequently a seq cannot identify a request across retries. The gateway tracks
  the seq of the current transmission and updates it on every retransmit.

### Flags byte

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | ACK_REQ | Receiver must send ACK/NACK |
| 1 | ENCRYPTED | Payload + GCM tag present (always 1 when a node key is provisioned) |
| 2 | FRAGMENTED | Payload spans multiple frames |
| 3 | DIR_DOWN | Gateway → node frame (part of the nonce) |
| 4–7 | RESERVED | Must be 0 |

### Frame types

| ID | Name | Reliable | Payload structure |
|----|------|----------|-------------------|
| 0x0 | SENSOR_DATA | fire & forget | N × (1B sensor_type + 4B Q31) |
| 0x1 | SENSOR_DATA_ACK | optional | last_seq (2B LE) |
| 0x2 | RPC_CMD | ACK + retry | cmd_id (1B) + param_len (1B) + params |
| 0x3 | RPC_RESP | — | cmd_id (1B) + status (1B) + data |
| 0x4 | FOTA_CHUNK | ACK + retry | chunk_offset (4B LE) + chunk_data |
| 0x5 | FOTA_CHUNK_ACK | — | chunk_offset (4B LE) + status (1B) |
| 0x6 | (reserved) | — | formerly OTA pairing beacon, unused |
| 0x7 | (reserved) | — | formerly OTA pairing response, unused |
| 0x8 | ACK/NACK | — | ack_seq (4B LE) + status (1B) |
| 0x9–0xF | RESERVED | — | future expansion |

### Frame budget by spreading factor

| SF / BW | Max frame | Overhead | Payload | Readings (5B each) | Min interval (1% duty) |
|---------|-----------|----------|---------|---------------------|------------------------|
| SF12/BW125 | 51 B | 24 B | 27 B | 5 | 99 s |
| SF11/BW125 | 51 B | 24 B | 27 B | 5 | 58 s |
| SF10/BW125 | 51 B | 24 B | 27 B | 5 | 37 s |
| SF9/BW125 | 115 B | 24 B | 91 B | 18 | 18.5 s |
| SF8/BW125 | 222 B | 24 B | 198 B | 39 | 10.3 s |
| SF7/BW125 | 222 B | 24 B | 198 B | 39 | 6.2 s |
| SF7/BW500 | 255 B | 24 B | 227 B | 46 | 5.6 s |

Default for sensor data: SF10/BW125. For FOTA: SF7/BW500.

---

## Internal Modules

### Radio Driver (`lora_drv_*.c`, `lora_radio_main.c`)

SPI-based HAL for SX1276/SX1262. Provides:

- TX/RX FIFO management
- Channel activity detection (CAD)
- RSSI / SNR readback
- Interrupt-driven RX via GPIO
- Configurable SF / BW via Kconfig

Abstracted behind a `struct lora_radio_ops` to allow a fake radio backend
(`CONFIG_LORA_RADIO_FAKE`) for native_sim testing.

### Packet Framing (`lib/lora_proto`)

One codec shared by the node (`lora_node`) and the gateway (`lora_radio`),
`<lora_proto/lora_packet.h>`:

- `lora_packet_encode(hdr, payload, len, key, out, out_size, &out_len)` — header
  + payload (AES-128-GCM via PSA when `key != NULL`, 12-byte tag) + CRC16;
  `-EMSGSIZE` if it does not fit `out_size`
- `lora_packet_decode(in, in_len, key, &hdr, payload, payload_size, &payload_len)`
  — checks CRC16 (`-EBADMSG`), authenticates and decrypts (`-EBADMSG` on a bad
  tag, header or key; `-EPERM` if encrypted and no key)
- `lora_hdr_seq_get/set()` — 32-bit seq accessors

`LORA_PROTO_MAX_READINGS(max_packet)` gives the readings per packet (5 at
SF12). Stateless: no global state besides the PSA key import per call.

### Session Manager (`lora_session.c`)

Maintains per-node state:

- `node_id` → `session_key` mapping
- `last_seq_rx` / `last_seq_tx` (sequence number tracking)
- `retry_count` and `last_ack_time` for reliable frames
- `state` enum in `lora_session.h`; sessions are created by `lora provision`
- Node ID allocation pool (0x0001–0x00FF for LoRa)
- Persistence to Zephyr Settings (`lora/<node_id>/key`)

On reboot: restores all sessions from Settings. Invalidates sessions where
GCM auth fails (the node was forgotten or its key rotated).

### Reliable TX (`lora_pending.c`)

Shared reliable transmission subsystem used by RPC and FOTA:

- Per-session pending-request slot (serialized per node)
- `k_work_delayable` retry timer with randomized exponential backoff
- Window size 1: one request in flight per node (a second send returns `-EBUSY`)
- ACK matching: a standalone ACK matches on `(node_id, ack_seq)` against the seq of the
  current transmission. Responses that carry the responder's own seq instead of
  echoing the request (FOTA_CHUNK_ACK) are validated by the handler (chunk offset)
  and complete the slot via `lora_pending_complete(node_id, frame_type)`
- On a match the retry work is rescheduled with `K_NO_WAIT`, so completion does not
  wait for the retry timer
- Retransmissions are re-encoded with a fresh seq (see Nonce); all transmissions go
  through `lora_radio_send()` (radio mutex, sliced RX)
- `lora_pending_send()` — blocking (for thread callers)
- `lora_pending_send_async()` — non-blocking (for zbus listeners)
- `lora_pending_ack_match()` — called from RX thread dispatch
- `lora_pending_cancel()` — called when session removed
- Timeout scaling: SF-dependent base timeout with ±30% jitter
- Max 3 retries, then `-ETIMEDOUT`
- Result published on `lora_rpc_result_chan` for async callers

### Protocol Handlers (`lora_handler_*.c`)

Dispatch table indexed by frame type byte. Each handler:

1. Validates GCM tag (if ENCRYPTED flag set)
2. Checks sequence number against session state (if ACK_REQ or reliable)
3. Dispatches to the correct path:
   - **data_handler**: unpacks N × 5B readings, calls `remote_sensor_publish_data()`
   - **rpc_handler**: dispatches to command table, builds RPC_RESP
   - **fota_handler**: writes chunk to `flash_img`, sends FOTA_CHUNK_ACK
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
| `remote_discovery_chan` | `remote_sensor` | Publishes: FOUND/LOST events on node discovery and session expiry |
| `config_cmd_chan` | `config_cmd` | Subscribes: new LORA_RPC command type → `lora_radio_rpc_send()` |

### New channels

| Channel | Owner | Direction | Message |
|---------|-------|-----------|---------|
| `remote_peer_cmd_chan` | `remote_sensor` | Manager → Transports | `remote_peer_cmd_event` (PEER_ADD, PEER_REMOVE, SEND_TRIGGER) |
| `lora_link_chan` | `lora_radio` | LoRa → Diagnostics | `lora_link_event` (RSSI, SNR, node_id, seq_num) |
| `lora_fota_chan` | `lora_radio` | HTTP → LoRa | `lora_fota_event` (FOTA_START, FOTA_CANCEL, image_size, chunk data) |
| `lora_rpc_result_chan` | `lora_radio` | LoRa → Consumers | `lora_rpc_result_event` (node_id, cmd_id, status, resp_data) |

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
    uint32_t seq_num;
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
/* In lib/lora_radio/src/lora_radio_main.c */
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

Nodes use a pre-shared per-node AES-128 key; there is no over-the-air pairing
(frame types 0x6/0x7 stay reserved, formerly OTA pairing).

1. On the gateway shell: `lora provision <node_id>` generates a 16-byte key with
   the CSPRNG, stores it in the session table (persisted via Settings) and prints
   it once as 32 hex chars.
2. On the node shell: `lora_node provision <node_id> <32-hex-key>` saves the
   session to Settings. `lora_node status` shows node id and paired state.
3. The node sends AES-128-GCM frames with a persisted write-ahead counter; the
   gateway drops plaintext and replayed frames (`lora sessions` lists node ids,
   last RX seq and replay drops, never keys).
4. `lora forget <node_id>` removes the session and its persisted key.

Native_sim dev nodes use `CONFIG_LORA_NODE_DEV_KEY`/`CONFIG_LORA_NODE_DEV_NODE_ID`
instead; never use those in production.

### Why pre-shared keys

The old OTA pairing had the gateway private key in source, sent the session key in clear and the node never verified the signature. PSK provisioning over the shell is simpler and actually secure.

### Security properties

| Property | Mechanism |
|----------|-----------|
| Key distribution | Pre-shared per-node key, entered via shell; never sent over the air |
| Replay protection | Per-direction sequence numbers, enforced before handlers run |
| Frame encryption | AES-128-GCM on all frames (plaintext rejected by default, `LORA_RADIO_REQUIRE_ENCRYPTION=y`) |
| Unpair | `lora forget <node_id>` removes the session and key; the gateway then drops that node's frames |

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

The node sends at most `CONFIG_SENSOR_NODE_TX_READINGS_PER_PACKET` (5) readings per frame; failed chunks are requeued; the buffer drops the oldest reading when full.

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

- RPC uses `lora_pending.c` shared reliable TX subsystem
- `lora_radio_rpc_send()` — blocking, waits for RPC_RESP or retries exhausted
- `lora_radio_rpc_send_async()` — non-blocking, result on `lora_rpc_result_chan`
- One pending RPC per node (serialized)
- SF-dependent timeout with ±30% randomization: 5s at SF7, 30s at SF12
- Exponential backoff: 1×, 2×, 4× timeout multiplier
- Up to 3 retries per command (Kconfig: `CONFIG_LORA_RADIO_RETRY_MAX`)
- No duplicate-command cache: replayed seqs are dropped by the receiver before the
  handler runs. A node retrying an RPC must use a new seq; the gateway answers
  every command that passes replay protection
- RPC_RESP serves as implicit ACK — standalone ACK (0x8) also supported

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
│                                          │  Verify image hash
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

- Window size 1 per node: the pending layer allows one chunk in flight, so the
  effective window is 1 regardless of `CONFIG_LORA_RADIO_FOTA_WINDOW`
- Every chunk and every retransmission of a chunk uses a fresh seq (new nonce)
- ACK matching is by chunk offset: the ACK echoes `offset` and carries the
  receiver's own seq, and retransmits change the chunk's seq, so seq cannot match.
  ACKs for an offset other than the in-flight chunk are ignored
- Each chunk uses `lora_pending_send_async()` for per-chunk reliability
- Retry timer: 2 seconds per chunk (Kconfig: `CONFIG_LORA_RADIO_FOTA_RETRY_MS`)
- 3 retries per chunk before cooldown
- Consecutive timeout cooldown: 30 seconds pause after 3 failures
  (Kconfig: `CONFIG_LORA_RADIO_FOTA_COOLDOWN_S`)
- Flash error: abort session immediately
- ACK carries written offset → gateway advances window
- Receiver dedup: ignores duplicate/out-of-order chunks

### FOTA modes

| Mode | Value | Description |
|------|-------|-------------|
| Full image | 0x00 | Write complete image to MCUboot slot 1 |
| cpatch delta | 0x01 | Apply binary patch to existing slot 1 image (future) |

### Duty cycle budget

| SF / BW | Chunk size | Airtime | Interval | 128 KB image |
|---------|------------|---------|----------|-------------|
| SF7/BW500 | 227 B | ~56 ms | 5.6 s | ~52 min |
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
CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE  — Max chunk data size (default 227)
CONFIG_LORA_RADIO_FOTA_WINDOW      — In-flight chunks (default 8)
CONFIG_LORA_RADIO_AUTO_PUBLISH_MS  — Periodic trigger forwarding (0 = disabled)
CONFIG_LORA_RADIO_RX_THREAD_STACK  — RX thread stack (default 2048)
CONFIG_LORA_SENSOR_PUBLISH_INTERVAL_S — Default publish interval (default 60)
CONFIG_LORA_SENSOR_KEEPALIVE_S     — Default keep-alive interval (default 0)
CONFIG_LORA_RADIO_DEFAULT_SF       — Default spreading factor (default 10)
CONFIG_LORA_RADIO_DEFAULT_BW       — Default bandwidth (default 125)
CONFIG_LORA_RADIO_SESSION_MAX      — Max provisioned nodes (default 16)
CONFIG_LORA_NODE_DEV_KEY / CONFIG_LORA_NODE_DEV_NODE_ID — native_sim development key only
```

---

## Future Extensions

| Feature | How it fits |
|---------|-------------|
| **UART proxy agent** | `CONFIG_ZBUS_PROXY_AGENT` + UART transport backend bridges gateway ↔ STM32WLE5JC channels. Shadow channels on gateway mirror `sensor_event_chan`, `lora_link_chan`, `remote_discovery_chan`, etc. Gateway consumers see no difference. Deferred to next ADR. |
| cpatch delta FOTA | `fota_mode = 0x01` in FOTA_START — no protocol change |
| New sensor types | Add `sensor_type` enum value — consumers ignore unknown types |
| New RPC commands | Add command ID in reserved range — no framing change |
| Mesh / repeater | New frame type + dst_node forwarding logic — header already supports it |
| LoRaWAN | Replace `lib/lora_radio/` internals — external surface (same zbus channels) unchanged |

---

## Related

- [ADR-015](../decisions.md) — architectural decision record
- [ADR-014](../decisions.md) — MCUboot FOTA
- [event-bus.md](event-bus.md) — zbus channel architecture
- [system-overview.md](system-overview.md) — system layers and library roles
