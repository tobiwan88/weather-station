# ADR-006 — LoRa Module Channel Boundary

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

The system uses LoRa point-to-point (no LoRaWAN) for the outdoor sensor node
to transmit weather data to the gateway. The gateway runs a LoRa receiver loop
and must integrate received data into the same data flow as locally-attached
sensors.

Several design questions arise:

1. Should `lora_rx_chan` carry raw LoRa packet bytes, a LoRa-specific decoded
   struct, or plain `env_sensor_data` events?

2. Should consumers (display, MQTT) know whether a reading came from LoRa or
   a local sensor?

3. Where does RSSI, SNR, and node sequence number information belong?

4. How does the LoRa TX side on the sensor node interact with the same zbus
   architecture?

Getting this wrong creates either a leaky abstraction (consumers specialise for
LoRa) or information loss (RSSI/SNR diagnostics discarded entirely).

---

## Decision

The `lora_radio` library is a **bounded context**. Everything LoRa-specific —
packet framing, RSSI, SNR, node IDs, sequence numbers, radio configuration —
is contained within `lib/lora_radio/`. Only `env_sensor_data` events cross
the module boundary onto `sensor_event_chan`. Link diagnostics travel on a
**separate** `lora_link_chan`.

### Boundary diagram

```
                    ┌─────────────────────────────────┐
                    │       lib/lora_radio/            │
                    │                                  │
  Radio hardware ──►│  SX1276/SX1262 driver            │
  (SPI/DIO pins)    │       │                          │
                    │  lora_recv()                     │
                    │       │                          │
                    │  lora_packet_decode()            │
                    │  ┌────┴──────────────────────┐   │
                    │  │  struct lora_weather_pkt  │   │
                    │  │  .node_id                 │   │
                    │  │  .seq_num                 │   │
                    │  │  .rssi  ◄── INTERNAL      │   │
                    │  │  .snr   ◄── INTERNAL      │   │
                    │  │  .samples[]               │   │
                    │  └────┬──────────┬───────────┘   │
                    │       │          │                │
                    └───────┼──────────┼────────────────┘
                            │          │
              env_sensor_data ×N   lora_link_info
                            │          │
                            ▼          ▼
                    sensor_event_chan  lora_link_chan
                            │          │
              ┌─────────────┤          └──► [logger / diagnostics]
              │             │               [future: RSSI display tile]
              ▼             ▼
       [display_manager] [mqtt_manager]
       (no idea this     (no idea this
        came from LoRa)   came from LoRa)
```

### LoRa wire format (internal to `lib/lora_radio/`)

```c
/* lib/lora_radio/src/lora_packet.c — never exposed in public headers */

struct __packed lora_weather_packet {
    uint8_t  node_id;       /* 1-byte node identity */
    uint16_t seq_num;       /* wrap-around sequence, detect loss */
    uint8_t  n_samples;     /* number of lora_sample entries following */
};

struct __packed lora_sample {
    uint32_t sensor_uid;    /* matches DT sensor-uid on remote node */
    uint8_t  type;          /* enum sensor_type */
    int32_t  q31_value;     /* same Q31 encoding as local sensors */
};
/* Packet = lora_weather_packet header + n_samples × lora_sample */
/* Max packet: 4 samples = 4 + 4×9 = 40 bytes — well within LoRa 255B limit */
```

`sensor_uid` is assigned in the remote node's devicetree. The gateway's
`sensor_registry` is pre-populated with the remote node's UIDs so that
display routing and MQTT topic naming work without any runtime negotiation.

### Decoding: one packet → N events

```c
void lora_rx_thread(void)
{
    uint8_t  buf[64];
    int16_t  rssi;
    int8_t   snr;

    while (1) {
        int len = lora_recv(lora_dev, buf, sizeof(buf),
                            K_FOREVER, &rssi, &snr);
        if (len < 0) continue;

        /* 1. Decode each sample → individual env_sensor_data events */
        struct lora_weather_packet *hdr = (void *)buf;
        struct lora_sample *samples = (void *)(buf + sizeof(*hdr));

        for (int i = 0; i < hdr->n_samples; i++) {
            struct env_sensor_data evt = {
                .sensor_uid   = samples[i].sensor_uid,
                .type         = samples[i].type,
                .q31_value    = samples[i].q31_value,
                .timestamp_ms = k_uptime_get(),  /* local clock */
            };
            zbus_chan_pub(&sensor_event_chan, &evt, K_MSEC(200));
        }

        /* 2. Publish link diagnostics on separate channel */
        struct lora_link_info link = {
            .node_id      = hdr->node_id,
            .seq_num      = hdr->seq_num,
            .rssi         = rssi,
            .snr          = snr,
            .timestamp_ms = k_uptime_get(),
        };
        zbus_chan_pub(&lora_link_chan, &link, K_MSEC(100));
    }
}
```

The timestamp on received events uses the **gateway's local clock**, not the
sensor node's clock. Clocks are not synchronised in this design. This is
acceptable for display and MQTT — the data is "just received, so recent".
If precise timestamps are needed in future, a simple sequence-number scheme
or NTP can be added without changing the event struct.

### TX side (sensor node)

On the sensor node, the flow is reversed. The sensor node:
1. Subscribes to `sensor_trigger_chan` (periodic timer publishes it)
2. Collects pending `env_sensor_data` events into a batch (or listens on
   `sensor_event_chan` directly)
3. Encodes the batch into a `lora_weather_packet`
4. Calls `lora_send()`

```
sensor_trigger_chan
        │
        ▼ (sensor drivers respond)
sensor_event_chan
        │
        ▼ (lora_tx listener batches events)
lora_packet_encode() → lora_send()
        │
        ▼
Radio hardware → over the air → gateway lora_recv()
```

### LoRa radio configuration (both nodes)

Both nodes must share configuration. This is enforced by defining the config
in a single shared header in `lib/lora_radio/include/lora_radio/lora_radio.h`:

```c
/* Shared between TX and RX nodes — must match exactly */
#define LORA_FREQUENCY_HZ    868100000   /* EU868 channel 1 */
#define LORA_BANDWIDTH       BW_125_KHZ
#define LORA_SPREADING       SF_10
#define LORA_CODING_RATE     CR_4_5
#define LORA_PREAMBLE_LEN    8
#define LORA_TX_POWER_DBM    14
#define LORA_PUBLIC_NETWORK  false       /* private sync word */
```

### `lora_link_chan` — diagnostic data

```c
/* include/common/lora_link.h */
struct lora_link_info {
    uint8_t  node_id;
    uint16_t seq_num;
    int16_t  rssi;       /* dBm */
    int8_t   snr;        /* dB */
    int64_t  timestamp_ms;
};
ZBUS_CHAN_DECLARE(lora_link_chan);
```

Initially, only the logger subscribes to `lora_link_chan`. Future consumers
(signal strength display tile, loss counter) subscribe without any changes to
the LoRa module.

---

## Consequences

**Easier:**
- Replacing the radio (SX1276 → SX1262, or eventually LoRaWAN) requires
  changing only `lib/lora_radio/` — all consumers are unaffected.
- RSSI/SNR are available for diagnostics without polluting the sensor data stream.
- The display and MQTT publisher work identically regardless of whether a
  reading arrived via LoRa, local I2C, or the fake sensor shell.
- Adding Thread or BLE remote sensors follows the same pattern: a new lib that
  receives packets and publishes `env_sensor_data` to `sensor_event_chan`.

**Harder:**
- The gateway must pre-know the remote sensor UIDs to populate `sensor_registry`.
  There is no auto-discovery protocol. For a small home weather station this
  is acceptable; for a large deployment it would need a registration flow.
- Packet loss results in stale display data. A "last received" timestamp
  display or a staleness indicator in the UI would require the display to track
  per-uid last-seen time. This is a future enhancement.

**Constrained:**
- The maximum packet size at SF10/BW125 is 222 bytes. The current format
  supports up to ~24 samples per packet — well beyond the 2–4 sensors
  in v1. This is not a practical constraint.
- EU868 1% duty cycle limits transmit time. At SF10/BW125 with a 40-byte
  packet, the on-air time is ~370ms → minimum interval ~37 seconds.
  `CONFIG_SENSOR_POLL_INTERVAL_S` must be ≥ 60 for EU868 compliance.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Expose raw LoRa bytes on a `lora_rx_chan` | Consumers must parse LoRa packets — couples display and MQTT to LoRa internals |
| Single `lora_rx_chan` with a LoRa-specific struct containing sensor data + RSSI | Consumers (display, MQTT) receive RSSI they don't need; LoRa concepts leak into consumer code |
| Merge RSSI/SNR into `env_sensor_data` | Sensor data struct gains radio-specific fields that make no sense for local sensors |
| LoRaWAN (TTN/ChirpStack) | Requires gateway hardware and network server; adds complexity and Internet dependency for a home LAN system |
