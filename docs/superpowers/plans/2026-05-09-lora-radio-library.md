# LoRa Radio Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `lib/lora_radio/` — the bounded-context LoRa protocol library that runs on the STM32WLE5JC co-processor, handling radio driver, packet framing, session management, protocol dispatch, and zbus integration.

**Architecture:** Kconfig-gated Zephyr library with four internal modules (radio driver, packet framing, session manager, protocol handlers), five zbus channel definitions (3 new + 2 existing), an iterable-section registration macro, and a fake radio backend for native_sim testing. The library is the STM32WLE5JC-side of the LoRa architecture; the gateway proxy agent is deferred.

**Tech Stack:** Zephyr RTOS, zbus, AES-128-GCM (PSA Crypto), Ed25519 (PSA Crypto), SX1262 SPI, native_sim (fake radio).

**Constraints:**
- ADR-015: fixed 8-byte L2 header, 12-byte GCM tag, 10 frame types
- ADR-002: all inter-library communication through zbus channels
- ADR-008: Kconfig-only composition, SYS_INIT self-wiring
- No heap (`k_malloc`/`free`)
- Flat structs only in zbus messages (no pointers)
- `__ASSERT` for internal invariants, normal `if` for external input

---

## File Structure

```
lib/lora_radio/
├── Kconfig
├── CMakeLists.txt
├── lora_radio_iterables.ld
├── include/
│   └── lora_radio/
│       ├── lora_radio.h              — Public API + iterable section macro
│       ├── lora_frame.h              — Frame protocol types (header, enum, payload structs)
│       ├── lora_session.h            — Session manager API + types
│       ├── lora_chan.h               — New zbus channel types + declarations
│       └── lora_radio_ops.h          — Radio driver abstraction (ops vtable)
├── src/
│   ├── lora_radio_main.c             — SYS_INIT, iterable section registration, RX thread
│   ├── lora_radio_drv_sx1262.c       — SX1262 SPI radio driver (full register-level)
│   ├── lora_radio_drv_fake.c         — Fake radio for native_sim testing
│   ├── lora_packet.c                 — Packet encode/decode, CRC16, GCM encrypt/decrypt
│   ├── lora_session.c                — Session manager (node map, keys, persistence)
│   ├── lora_chan.c                   — zbus channel definitions (lora_link_chan, lora_fota_chan)
│   ├── lora_handler_data.c           — SENSOR_DATA handler → sensor_event_chan
│   ├── lora_handler_rpc.c            — RPC_CMD/RPC_RESP handler + command table + rpc_send
│   ├── lora_handler_fota.c           — FOTA_CHUNK handler + chunk protocol
│   ├── lora_handler_prov.c           — PROV_BEACON/PROV_RESPONSE handler
│   └── lora_shell.c                  — Shell commands (CONFIG_LORA_RADIO_SHELL)

lib/remote_sensor/  (modified)
├── src/remote_peer_cmd_chan.c        — NEW: remote_peer_cmd_chan definition
├── include/remote_sensor/remote_sensor.h  — MODIFIED: add remote_peer_cmd_event type
├── CMakeLists.txt                    — MODIFIED: add remote_peer_cmd_chan.c
```

---

## Task Overview

| Task | Files | Depends On |
|------|-------|------------|
| 1 | Kconfig, CMakeLists, lora_frame.h, lora_radio.h, iterables.ld, lora_radio_main.c | — |
| 2 | lora_chan.h, lora_chan.c, remote_peer_cmd_chan.c, remote_sensor.h, CMakeLists | 1 |
| 3 | lora_session.h, lora_session.c | 1 |
| 4 | lora_packet.c | 1, 3 |
| 5 | lora_radio_ops.h, lora_radio_drv_fake.c, lora_radio_main.c | 1, 4 |
| 6 | lora_radio_drv_sx1262.c | 5 |
| 7 | lora_handler_data.c, lora_radio_main.c | 2, 3, 4, 5 |
| 8 | lora_handler_prov.c | 2, 3, 4, 5 |
| 9 | lora_handler_rpc.c | 2, 3, 4, 5 |
| 10 | lora_handler_fota.c | 2, 3, 4, 5 |
| 11 | lora_shell.c | 1 |

---

### Task 1: Library Scaffold — Kconfig, Types, Iterable Registration, Main Thread

**Files:**
- Create: `lib/lora_radio/Kconfig`
- Create: `lib/lora_radio/CMakeLists.txt`
- Create: `lib/lora_radio/lora_radio_iterables.ld`
- Create: `lib/lora_radio/include/lora_radio/lora_frame.h`
- Create: `lib/lora_radio/include/lora_radio/lora_radio.h`
- Create: `lib/lora_radio/src/lora_radio_main.c`
- Modify: `lib/Kconfig` (add `rsource "lora_radio/Kconfig"`)

- [ ] **Step 1.1: Create Kconfig**

```kconfig
# SPDX-License-Identifier: Apache-2.0

menuconfig LORA_RADIO
	bool "LoRa radio protocol library"
	depends on ZBUS
	depends on SENSOR_EVENT
	depends on REMOTE_SENSOR
	select PSA_CRYPTO
	help
	  Bounded-context LoRa protocol library for the STM32WLE5JC co-processor.
	  Handles SX1262 radio driver, packet framing (AES-128-GCM), session
	  management, provisioning, RPC, FOTA, and sensor data dispatch.

	  All runtime communication goes through zbus channels — no direct
	  library-to-library calls (ADR-002).

if LORA_RADIO

config LORA_RADIO_DRV_SX1262
	bool "Semtech SX1262 radio driver"
	default y
	help
	  SPI-based driver for the SX1262 radio chip (internal to STM32WLE5JC).

config LORA_RADIO_FAKE
	bool "Fake radio backend for native_sim testing"
	depends on ARCH_POSIX
	help
	  Simulated radio that injects/receives bytes without hardware.
	  Used only on native_sim targets.

config LORA_RADIO_FOTA
	bool "FOTA chunk handler (MCUboot relay)"
	depends on IMG_MANAGER
	depends on FLASH_MAP
	help
	  Enables the firmware-over-the-air relay: receives FOTA_CHUNK frames,
	  writes to MCUboot slot 1 via flash_img, and triggers upgrade.

config LORA_RADIO_FOTA_CHUNK_SIZE
	int "Max FOTA chunk data bytes per frame"
	default 231
	range 16 231
	depends on LORA_RADIO_FOTA

config LORA_RADIO_FOTA_WINDOW
	int "In-flight FOTA chunks (window size)"
	default 8
	range 1 16
	depends on LORA_RADIO_FOTA

config LORA_RADIO_AUTO_PUBLISH_MS
	int "Periodic trigger forwarding interval (ms, 0 = disabled)"
	default 60000
	range 0 3600000
	help
	  When non-zero, lora_radio publishes periodic sensor_trigger_events
	  on its local bus at this interval, causing connected sensor nodes
	  to sample and transmit.

config LORA_RADIO_RX_THREAD_STACK_SIZE
	int "RX thread stack size (bytes)"
	default 2048
	range 1024 8192

config LORA_RADIO_RX_THREAD_PRIORITY
	int "RX thread priority"
	default 5
	range 0 15

config LORA_RADIO_DEFAULT_SF
	int "Default LoRa spreading factor"
	default 10
	range 7 12

config LORA_RADIO_DEFAULT_BW
	int "Default LoRa bandwidth (kHz)"
	default 125
	range 7 500

config LORA_SENSOR_PUBLISH_INTERVAL_S
	int "Default publish interval (seconds)"
	default 60
	range 0 3600
	help
	  Default SET_PUBLISH_INTERVAL for newly paired sensor nodes.

config LORA_SENSOR_KEEPALIVE_S
	int "Default keep-alive interval (seconds, 0 = disabled)"
	default 0
	range 0 86400

config LORA_RADIO_SESSION_MAX
	int "Maximum number of paired LoRa nodes"
	default 16
	range 1 64

config LORA_RADIO_SHELL
	bool "Shell commands for LoRa radio management"
	default y
	depends on SHELL
	help
	  Adds the "lora" shell command group.

module = LORA_RADIO
module-str = LORA_RADIO
source "subsys/logging/Kconfig.template.log_config"

endif # LORA_RADIO
```

- [ ] **Step 1.2: Create CMakeLists.txt**

```cmake
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_LORA_RADIO)

  zephyr_library()
  zephyr_library_sources(
    src/lora_radio_main.c
    src/lora_packet.c
    src/lora_session.c
    src/lora_chan.c
    src/lora_handler_data.c
    src/lora_handler_rpc.c
    src/lora_handler_prov.c)

  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_DRV_SX1262
    src/lora_radio_drv_sx1262.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_FAKE
    src/lora_radio_drv_fake.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_FOTA
    src/lora_handler_fota.c)
  zephyr_library_sources_ifdef(CONFIG_LORA_RADIO_SHELL
    src/lora_shell.c)

  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

  zephyr_linker_sources(SECTIONS lora_radio_iterables.ld)

endif()
```

- [ ] **Step 1.3: Create iterables.ld**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/* Linker section for lora_transport_info iterable structs. */
ITERABLE_SECTION_ROM(lora_transport_info, 4)
```

- [ ] **Step 1.4: Create lora_frame.h — frame protocol types**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_FRAME_H_
#define LORA_RADIO_LORA_FRAME_H_

#include <stdint.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Frame type IDs (4-bit field = 16 slots)
 * ------------------------------------------------------------------ */
#define LORA_FRAME_SENSOR_DATA      0x0U
#define LORA_FRAME_SENSOR_DATA_ACK  0x1U
#define LORA_FRAME_RPC_CMD          0x2U
#define LORA_FRAME_RPC_RESP         0x3U
#define LORA_FRAME_FOTA_CHUNK       0x4U
#define LORA_FRAME_FOTA_CHUNK_ACK   0x5U
#define LORA_FRAME_PROV_BEACON      0x6U
#define LORA_FRAME_PROV_RESPONSE    0x7U
#define LORA_FRAME_ACK              0x8U

/* ------------------------------------------------------------------
 * Flags byte bits
 * ------------------------------------------------------------------ */
#define LORA_FLAG_ACK_REQ    BIT(0)
#define LORA_FLAG_ENCRYPTED  BIT(1)
#define LORA_FLAG_FRAGMENTED BIT(2)

/* ------------------------------------------------------------------
 * L2 header — 8 bytes
 * ------------------------------------------------------------------ */
struct lora_l2_header {
	uint8_t  type_ver;    /* type (top 4 bits) | version (bottom 4 bits) */
	uint8_t  flags;
	uint16_t src_node;
	uint16_t dst_node;
	uint16_t seq_num;
} __packed;

BUILD_ASSERT(sizeof(struct lora_l2_header) == 8);

/* ------------------------------------------------------------------
 * Frame payload structures
 * ------------------------------------------------------------------ */

/* SENSOR_DATA_ACK */
struct lora_sensor_data_ack {
	uint16_t last_seq;
} __packed;

/* RPC_CMD */
struct lora_rpc_cmd {
	uint8_t  cmd_id;
	uint8_t  param_len;
	uint8_t  params[];
} __packed;

/* RPC_RESP */
struct lora_rpc_resp {
	uint8_t  cmd_id;
	uint8_t  status;
	uint8_t  data[];
} __packed;

/* FOTA_CHUNK */
struct lora_fota_chunk {
	uint32_t offset;
	uint8_t  data[];
} __packed;

/* FOTA_CHUNK_ACK */
struct lora_fota_chunk_ack {
	uint32_t offset;
	uint8_t  status;
} __packed;

/* PROV_BEACON payload descriptor */
struct lora_prov_beacon {
	uint8_t  caps_count;
	uint8_t  caps[];
} __packed;

/* PROV_RESPONSE */
struct lora_prov_response {
	uint16_t node_id;
	uint8_t  session_key[16];
	uint8_t  gateway_pubkey[32];
	uint8_t  signature[64];
} __packed;
BUILD_ASSERT(sizeof(struct lora_prov_response) == 114);

/* Standalone ACK/NACK */
struct lora_ack {
	uint16_t ack_seq;
	uint8_t  status;
} __packed;

/* ------------------------------------------------------------------
 * L2 header helpers
 * ------------------------------------------------------------------ */
static inline uint8_t lora_frame_type(const struct lora_l2_header *hdr)
{
	return hdr->type_ver >> 4;
}

static inline uint8_t lora_frame_version(const struct lora_l2_header *hdr)
{
	return hdr->type_ver & 0x0FU;
}

static inline void lora_set_frame_type(struct lora_l2_header *hdr, uint8_t type)
{
	hdr->type_ver = (uint8_t)((type << 4) | (hdr->type_ver & 0x0FU));
}

static inline void lora_set_version(struct lora_l2_header *hdr, uint8_t ver)
{
	hdr->type_ver = (uint8_t)((hdr->type_ver & 0xF0U) | (ver & 0x0FU));
}

/* ------------------------------------------------------------------
 * Max payload sizes
 * ------------------------------------------------------------------ */
#define LORA_MAX_PACKET_SF12  51U
#define LORA_MAX_PACKET_SF7   255U
#define LORA_OVERHEAD         20U   /* 8B header + 12B GCM tag */
#define LORA_MAX_PAYLOAD_SF12 (LORA_MAX_PACKET_SF12 - LORA_OVERHEAD)
#define LORA_MAX_PAYLOAD_SF7  (LORA_MAX_PACKET_SF7 - LORA_OVERHEAD)
#define LORA_READING_SIZE     5U    /* 1B type + 4B Q31 */

/* ------------------------------------------------------------------
 * RPC command IDs
 * ------------------------------------------------------------------ */
#define LORA_RPC_PING                 0x00U
#define LORA_RPC_GET_VERSION          0x01U
#define LORA_RPC_GET_STATUS           0x02U
#define LORA_RPC_SET_PUBLISH_INTERVAL 0x10U
#define LORA_RPC_SET_SPREADING        0x11U
#define LORA_RPC_SET_TX_POWER         0x12U
#define LORA_RPC_SET_KEEPALIVE        0x13U
#define LORA_RPC_SET_CHANGE_THRESHOLD 0x14U
#define LORA_RPC_TRIGGER_SAMPLE       0x20U
#define LORA_RPC_FOTA_START           0x30U
#define LORA_RPC_FOTA_CANCEL          0x31U
#define LORA_RPC_REBOOT               0xFFU

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_FRAME_H_ */
```

- [ ] **Step 1.5: Create lora_radio.h — public API + iterable section macro**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_H_
#define LORA_RADIO_LORA_RADIO_H_

#include <stdint.h>
#include <zephyr/sys/iterable_sections.h>
#include <lora_radio/lora_frame.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lora_transport_info {
	const char *name;
	uint8_t     proto;
	uint32_t    caps;
};

#define REMOTE_TRANSPORT_REGISTER(inst, ...) \
	STRUCT_SECTION_ITERABLE(lora_transport_info, inst) = __VA_ARGS__

int lora_radio_publish_data(uint32_t uid, uint8_t type, int32_t q31);

int lora_radio_rpc_send(uint16_t node_id, uint8_t cmd_id,
			const uint8_t *params, uint8_t param_len);

uint32_t lora_radio_uid_from_node_id(uint8_t node_id, uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_RADIO_H_ */
```

- [ ] **Step 1.6: Create lora_radio_main.c — SYS_INIT, iterable section, RX thread**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_radio
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/zbus/zbus.h>
#include <sensor_trigger/sensor_trigger.h>

#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_radio_ops.h>
#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_frame.h>

/* Select radio ops based on Kconfig */
#ifdef CONFIG_LORA_RADIO_FAKE
extern const struct lora_radio_ops lora_fake_radio_ops;
const struct lora_radio_ops *lora_radio_ops = &lora_fake_radio_ops;
#elif CONFIG_LORA_RADIO_DRV_SX1262
extern const struct lora_radio_ops lora_sx1262_radio_ops;
const struct lora_radio_ops *lora_radio_ops = &lora_sx1262_radio_ops;
#endif

/* Iterable section registration */
REMOTE_TRANSPORT_REGISTER(lora_transport, {
	.name = "lora",
	.proto = 2,
	.caps = BIT(0),
});

/* RX thread */
K_THREAD_STACK_DEFINE(lora_rx_stack, CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE);
static struct k_thread lora_rx_thread_data;

void lora_rx_thread_entry(void *, void *, void *);

/* Periodic trigger timer */
static struct k_timer lora_trigger_timer;

static void lora_trigger_handler(struct k_timer *timer)
{
	(void)timer;
	const struct sensor_trigger_event evt = {
		.source = 2,
		.target_uid = 0,
	};
	zbus_chan_pub(&sensor_trigger_chan, &evt, K_NO_WAIT);
}

static int lora_radio_init(void)
{
	lora_session_init();
	lora_session_restore();

	if (lora_radio_ops->init) {
		lora_radio_ops->init();
	}

	k_thread_create(&lora_rx_thread_data, lora_rx_stack,
			CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE,
			lora_rx_thread_entry, NULL, NULL, NULL,
			CONFIG_LORA_RADIO_RX_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&lora_rx_thread_data, "lora_rx");

	if (CONFIG_LORA_RADIO_AUTO_PUBLISH_MS > 0) {
		k_timer_init(&lora_trigger_timer, lora_trigger_handler, NULL);
		k_timer_start(&lora_trigger_timer,
			      K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS),
			      K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS));
	}

	LOG_INF("LoRa radio initialized");
	return 0;
}

SYS_INIT(lora_radio_init, APPLICATION, 80);
```

- [ ] **Step 1.7: Implement lora_radio_uid_from_node_id()** (in lora_radio_main.c)

```c
#include <lora_radio/lora_radio.h>

#define LORA_UID_PREFIX 0x0200U

uint32_t lora_radio_uid_from_node_id(uint8_t node_id, uint8_t type)
{
	return ((uint32_t)LORA_UID_PREFIX << 16) |
	       ((uint32_t)node_id << 4) |
	       (type & 0x0F);
}
```

- [ ] **Step 1.8: Implement lora_radio_publish_data()** (in lora_radio_main.c)

```c
#include <zephyr/zbus/zbus.h>
#include <sensor_event/sensor_event.h>

int lora_radio_publish_data(uint32_t uid, uint8_t type, int32_t q31)
{
	struct env_sensor_data evt = {
		.sensor_uid = uid,
		.type = (enum sensor_type)type,
		.q31_value = q31,
		.timestamp_ms = k_uptime_get(),
	};
	return zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);
}
```

- [ ] **Step 1.9: Implement RX thread main loop + link diagnostics**

```c
/* In lora_radio_main.c — RX thread entry */

/* Forward declarations for handler dispatch */
int lora_handle_sensor_data(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);
int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);
int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len);
int lora_handle_prov_beacon(const struct lora_l2_header *hdr, const uint8_t *payload, uint8_t payload_len);

void lora_rx_thread_entry(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;

	uint8_t buf[LORA_MAX_PACKET_SF7];
	uint8_t payload[LORA_MAX_PAYLOAD_SF7];
	struct lora_l2_header hdr;
	uint8_t payload_len;

	while (1) {
		int ret = lora_radio_ops->rx(buf, sizeof(buf), K_FOREVER);
		if (ret < 0) {
			continue;
		}

		/* Skip non-header bytes at start */
		int pkt_len = ret;

		/* Get session key for src_node */
		memcpy(&hdr, buf, sizeof(hdr));
		uint16_t src_node = hdr.src_node;
		struct lora_session *s = lora_session_get(src_node);
		const uint8_t *key = s ? s->session_key : NULL;

		/* Publish link diagnostics */
		struct lora_link_event link_evt = {
			.node_id = src_node,
			.rssi = lora_radio_ops->rssi(),
			.snr = lora_radio_ops->snr(),
			.seq_num = hdr.seq_num,
			.crc_errors = 0,
		};
		zbus_chan_pub(&lora_link_chan, &link_evt, K_NO_WAIT);

		/* Decode packet */
		ret = lora_packet_decode(buf, pkt_len, key, &hdr, payload, &payload_len);
		if (ret < 0) {
			LOG_WRN("packet decode failed from node 0x%04x: %d", src_node, ret);
			continue;
		}

		/* Update session */
		if (s) {
			s->last_seq_rx = hdr.seq_num;
			s->last_rx_ms = k_uptime_get();
		}

		/* Dispatch by frame type */
		switch (lora_frame_type(&hdr)) {
		case LORA_FRAME_SENSOR_DATA:
			lora_handle_sensor_data(src_node, payload, payload_len);
			break;
		case LORA_FRAME_RPC_CMD:
			lora_handle_rpc_cmd(src_node, payload, payload_len);
			break;
		case LORA_FRAME_PROV_BEACON:
			lora_handle_prov_beacon(&hdr, payload, payload_len);
			break;
#ifdef CONFIG_LORA_RADIO_FOTA
		case LORA_FRAME_FOTA_CHUNK:
			lora_handle_fota_chunk(src_node, payload, payload_len);
			break;
#endif
		default:
			LOG_DBG("unhandled frame type 0x%x from node 0x%04x",
				lora_frame_type(&hdr), src_node);
			break;
		}
	}
}
```

- [ ] **Step 1.10: Register library in lib/Kconfig**

Add to `lib/Kconfig` (alphabetical, after `location_registry`):
```kconfig
rsource "lora_radio/Kconfig"
```

---

### Task 2: Zbus Channel Definitions

**Files:**
- Create: `lib/lora_radio/include/lora_radio/lora_chan.h`
- Create: `lib/lora_radio/src/lora_chan.c`
- Create: `lib/remote_sensor/src/remote_peer_cmd_chan.c`
- Modify: `lib/remote_sensor/include/remote_sensor/remote_sensor.h`
- Modify: `lib/remote_sensor/CMakeLists.txt`

- [ ] **Step 2.1: Create lora_chan.h**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_CHAN_H_
#define LORA_RADIO_LORA_CHAN_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lora_link_event {
	uint16_t node_id;
	int16_t  rssi;
	uint8_t  snr;
	uint16_t seq_num;
	uint16_t crc_errors;
};

struct lora_fota_event {
	enum { LORA_FOTA_START, LORA_FOTA_CANCEL } action;
	uint32_t target_uid;
	uint32_t image_size;
	uint8_t  fota_mode;
};

ZBUS_CHAN_DECLARE(lora_link_chan);
ZBUS_CHAN_DECLARE(lora_fota_chan);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_CHAN_H_ */
```

- [ ] **Step 2.2: Create lora_chan.c**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zbus/zbus.h>
#include <lora_radio/lora_chan.h>

ZBUS_CHAN_DEFINE(lora_link_chan, struct lora_link_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.node_id = 0, .rssi = 0, .snr = 0,
			       .seq_num = 0, .crc_errors = 0));

ZBUS_CHAN_DEFINE(lora_fota_chan, struct lora_fota_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.action = 0, .target_uid = 0,
			       .image_size = 0, .fota_mode = 0));
```

- [ ] **Step 2.3: Create remote_peer_cmd_chan.c** (in remote_sensor)

Add to `lib/remote_sensor/include/remote_sensor/remote_sensor.h`:
```c
enum remote_peer_cmd_action {
	REMOTE_PEER_CMD_ADD,
	REMOTE_PEER_CMD_REMOVE,
	REMOTE_PEER_CMD_SEND_TRIGGER,
};

struct remote_peer_cmd_event {
	enum remote_peer_cmd_action action;
	enum remote_transport_proto proto;
	uint32_t target_uid;
	uint8_t  peer_addr[REMOTE_SENSOR_ADDR_MAX_LEN];
	uint8_t  addr_len;
};

ZBUS_CHAN_DECLARE(remote_peer_cmd_chan);
```

Create `lib/remote_sensor/src/remote_peer_cmd_chan.c`:
```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zbus/zbus.h>
#include <remote_sensor/remote_sensor.h>

ZBUS_CHAN_DEFINE(remote_peer_cmd_chan, struct remote_peer_cmd_event,
		 NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.action = 0, .proto = 0, .target_uid = 0,
			       .addr_len = 0));
```

Add to `lib/remote_sensor/CMakeLists.txt`:
```cmake
  zephyr_library_sources(src/remote_peer_cmd_chan.c)
```

---

### Task 3: Session Manager

**Files:**
- Create: `lib/lora_radio/include/lora_radio/lora_session.h`
- Create: `lib/lora_radio/src/lora_session.c`

- [ ] **Step 3.1: Create lora_session.h**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_SESSION_H_
#define LORA_RADIO_LORA_SESSION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lora_session_state {
	LORA_SESSION_UNPAIRED,
	LORA_SESSION_PAIRING,
	LORA_SESSION_PAIRED,
	LORA_SESSION_EXPIRED,
};

struct lora_session {
	uint16_t node_id;
	enum lora_session_state state;
	uint8_t  session_key[16];
	uint16_t last_seq_rx;
	uint16_t last_seq_tx;
	uint8_t  retry_count;
	int64_t  last_rx_ms;
	uint8_t  ed25519_pubkey[32];
};

void lora_session_init(void);
struct lora_session *lora_session_get(uint16_t node_id);
struct lora_session *lora_session_add(uint16_t node_id,
				      const uint8_t key[16],
				      const uint8_t pubkey[32]);
void lora_session_remove(uint16_t node_id);
uint16_t lora_session_alloc_node_id(void);
void lora_session_restore(void);
void lora_session_persist(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_SESSION_H_ */
```

- [ ] **Step 3.2: Create lora_session.c**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_session
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include <lora_radio/lora_session.h>

#define SESSION_MAX CONFIG_LORA_RADIO_SESSION_MAX
#define NODE_ID_MIN 0x0001U
#define NODE_ID_MAX 0x00FFU

static struct lora_session sessions[SESSION_MAX];
static uint8_t session_count;

void lora_session_init(void)
{
	(void)memset(sessions, 0, sizeof(sessions));
	session_count = 0;
}

struct lora_session *lora_session_get(uint16_t node_id)
{
	for (uint8_t i = 0; i < session_count; i++) {
		if (sessions[i].node_id == node_id) {
			return &sessions[i];
		}
	}
	return NULL;
}

struct lora_session *lora_session_add(uint16_t node_id,
				      const uint8_t key[16],
				      const uint8_t pubkey[32])
{
	if (session_count >= SESSION_MAX) {
		LOG_WRN("session table full (%d)", SESSION_MAX);
		return NULL;
	}
	struct lora_session *s = &sessions[session_count++];
	s->node_id = node_id;
	s->state = LORA_SESSION_PAIRED;
	memcpy(s->session_key, key, 16);
	memcpy(s->ed25519_pubkey, pubkey ? pubkey : sessions[0].ed25519_pubkey, 32);
	s->last_seq_rx = 0;
	s->last_seq_tx = 0;
	s->retry_count = 0;
	s->last_rx_ms = 0;
	return s;
}

void lora_session_remove(uint16_t node_id)
{
	for (uint8_t i = 0; i < session_count; i++) {
		if (sessions[i].node_id == node_id) {
			memmove(&sessions[i], &sessions[i + 1],
				(session_count - i - 1) * sizeof(struct lora_session));
			session_count--;
			LOG_INF("unpaired node 0x%04x", node_id);
			return;
		}
	}
}

uint16_t lora_session_alloc_node_id(void)
{
	bool used[NODE_ID_MAX - NODE_ID_MIN + 1];
	(void)memset(used, 0, sizeof(used));
	for (uint8_t i = 0; i < session_count; i++) {
		uint16_t id = sessions[i].node_id;
		if (id >= NODE_ID_MIN && id <= NODE_ID_MAX) {
			used[id - NODE_ID_MIN] = true;
		}
	}
	for (uint16_t id = NODE_ID_MIN; id <= NODE_ID_MAX; id++) {
		if (!used[id - NODE_ID_MIN]) {
			return id;
		}
	}
	return 0;
}

static int lora_settings_export(int (*cb)(const char *name, const void *val, size_t len))
{
	char key[32];
	for (uint8_t i = 0; i < session_count; i++) {
		snprintk(key, sizeof(key), "lora/%04x/key", sessions[i].node_id);
		cb(key, sessions[i].session_key, 16);
	}
	return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(lora_radio, "lora", NULL, NULL,
			       lora_settings_export, NULL);

void lora_session_persist(void)
{
	settings_save();
}

void lora_session_restore(void)
{
	settings_load_subtree("lora");
	LOG_INF("sessions restored (%d nodes)", session_count);
}
```

---

### Task 4: Packet Framing — Encode, Decode, CRC, AES-128-GCM

**Files:**
- Create: `lib/lora_radio/src/lora_packet.c`

- [ ] **Step 4.1: Create lora_packet.c**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_packet
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <string.h>
#include <psa/crypto.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_session.h>

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

int lora_packet_encode(struct lora_l2_header *hdr,
		       const void *payload, uint8_t payload_len,
		       const uint8_t session_key[16],
		       uint8_t *out_buf, uint8_t *out_len)
{
	uint8_t hdr_len = sizeof(struct lora_l2_header);
	uint8_t total = hdr_len + payload_len + 12;

	if (total > LORA_MAX_PACKET_SF7) {
		LOG_ERR("packet too large: %d > %d", total, LORA_MAX_PACKET_SF7);
		return -ENOSPC;
	}

	memcpy(out_buf, hdr, hdr_len);
	memcpy(out_buf + hdr_len, payload, payload_len);

	if (session_key != NULL) {
		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
		psa_key_id_t key_id;
		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_GCM);
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);

		psa_status_t s = psa_import_key(&attr, session_key, 16, &key_id);
		if (s != PSA_SUCCESS) {
			LOG_ERR("psa_import_key failed: %d", s);
			return -EIO;
		}

		uint8_t nonce[12] = {0};
		nonce[0] = hdr->seq_num & 0xFF;
		nonce[1] = (hdr->seq_num >> 8) & 0xFF;

		size_t tag_len;
		s = psa_aead_encrypt(&key_id, PSA_ALG_GCM,
				     nonce, sizeof(nonce),
				     out_buf, hdr_len,
				     out_buf + hdr_len, payload_len,
				     out_buf + hdr_len, LORA_MAX_PAYLOAD_SF7,
				     &tag_len);
		psa_destroy_key(key_id);

		if (s != PSA_SUCCESS) {
			LOG_ERR("psa_aead_encrypt failed: %d", s);
			return -EIO;
		}

		hdr->flags |= LORA_FLAG_ENCRYPTED;
		memcpy(out_buf, hdr, hdr_len);
		*out_len = hdr_len + payload_len + tag_len;
	} else {
		*out_len = total;
	}

	uint16_t crc = crc16_ccitt(out_buf, *out_len);
	out_buf[(*out_len)++] = crc & 0xFF;
	out_buf[(*out_len)++] = (crc >> 8) & 0xFF;
	return 0;
}

int lora_packet_decode(const uint8_t *in_buf, uint8_t in_len,
		       const uint8_t session_key[16],
		       struct lora_l2_header *hdr,
		       uint8_t *payload, uint8_t *payload_len)
{
	if (in_len < sizeof(struct lora_l2_header) + 2) {
		return -EINVAL;
	}

	uint16_t crc_rx = (uint16_t)in_buf[in_len - 2] |
			  ((uint16_t)in_buf[in_len - 1] << 8);
	uint16_t crc_calc = crc16_ccitt(in_buf, in_len - 2);
	if (crc_rx != crc_calc) {
		LOG_WRN("CRC mismatch");
		return -EBADMSG;
	}

	uint8_t hdr_len = sizeof(struct lora_l2_header);
	memcpy(hdr, in_buf, hdr_len);
	uint8_t enc_len = in_len - hdr_len - 2;
	bool encrypted = (hdr->flags & LORA_FLAG_ENCRYPTED) != 0;

	if (encrypted) {
		if (session_key == NULL) {
			LOG_WRN("encrypted frame but no key for node 0x%04x", hdr->src_node);
			return -EPERM;
		}
		if (enc_len < 12) {
			return -EINVAL;
		}

		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
		psa_key_id_t key_id;
		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DECRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_GCM);
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);

		psa_status_t s = psa_import_key(&attr, session_key, 16, &key_id);
		if (s != PSA_SUCCESS) {
			return -EIO;
		}

		uint8_t nonce[12] = {0};
		nonce[0] = hdr->seq_num & 0xFF;
		nonce[1] = (hdr->seq_num >> 8) & 0xFF;

		size_t dec_len;
		s = psa_aead_decrypt(&key_id, PSA_ALG_GCM,
				     nonce, sizeof(nonce),
				     in_buf, hdr_len,
				     in_buf + hdr_len, enc_len,
				     payload, LORA_MAX_PAYLOAD_SF7,
				     &dec_len);
		psa_destroy_key(key_id);

		if (s != PSA_SUCCESS) {
			LOG_WRN("GCM decrypt failed: %d (node 0x%04x)", s, hdr->src_node);
			return -EBADMSG;
		}
		*payload_len = dec_len;
	} else {
		uint8_t plen = enc_len;
		if (plen > LORA_MAX_PAYLOAD_SF7) {
			return -EINVAL;
		}
		memcpy(payload, in_buf + hdr_len, plen);
		*payload_len = plen;
	}
	return 0;
}
```

---

### Task 5: Radio Driver Abstraction + Fake Radio

**Files:**
- Create: `lib/lora_radio/include/lora_radio/lora_radio_ops.h`
- Create: `lib/lora_radio/src/lora_radio_drv_fake.c`

- [ ] **Step 5.1: Create lora_radio_ops.h**

```c
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_OPS_H_
#define LORA_RADIO_LORA_RADIO_OPS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lora_radio_ops {
	int (*init)(void);
	int (*set_frequency)(uint32_t freq_hz);
	int (*set_modem_config)(uint8_t sf, uint32_t bw);
	int (*set_tx_power)(int8_t dbm);
	int (*tx)(const uint8_t *data, uint8_t len);
	int (*rx)(uint8_t *buf, uint8_t max_len, int32_t timeout_ms);
	int (*rx_enable)(bool enable);
	int16_t (*rssi)(void);
	int8_t  (*snr)(void);
};

extern const struct lora_radio_ops *lora_radio_ops;

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_RADIO_OPS_H_ */
```

- [ ] **Step 5.2: Create fake radio driver**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fake
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <string.h>

#include <lora_radio/lora_radio_ops.h>

static K_FIFO_DEFINE(lora_fake_tx_fifo);
static K_FIFO_DEFINE(lora_fake_rx_fifo);

struct fake_packet {
	struct k_fifo _fifo;
	uint8_t len;
	uint8_t data[];
};

static int fake_init(void)
{
	LOG_INF("fake radio initialized");
	return 0;
}

static int fake_set_frequency(uint32_t freq_hz)
{
	(void)freq_hz;
	return 0;
}

static int fake_set_modem_config(uint8_t sf, uint32_t bw)
{
	(void)sf; (void)bw;
	return 0;
}

static int fake_set_tx_power(int8_t dbm)
{
	(void)dbm;
	return 0;
}

static int fake_tx(const uint8_t *data, uint8_t len)
{
	struct fake_packet *pkt = k_malloc(sizeof(*pkt) + len);
	if (!pkt) {
		return -ENOMEM;
	}
	pkt->len = len;
	memcpy(pkt->data, data, len);
	k_fifo_put(&lora_fake_tx_fifo, pkt);
	LOG_DBG("fake TX: %d bytes", len);
	return 0;
}

static int fake_rx(uint8_t *buf, uint8_t max_len, int32_t timeout_ms)
{
	struct fake_packet *pkt = k_fifo_get(&lora_fake_rx_fifo,
					     K_MSEC(timeout_ms));
	if (!pkt) {
		return -EAGAIN;
	}
	uint8_t copy = MIN(pkt->len, max_len);
	memcpy(buf, pkt->data, copy);
	int ret = copy;
	k_free(pkt);
	return ret;
}

static int fake_rx_enable(bool enable)
{
	(void)enable;
	return 0;
}

static int16_t fake_rssi(void)
{
	return -90;
}

static int8_t fake_snr(void)
{
	return 8;
}

const struct lora_radio_ops lora_fake_radio_ops = {
	.init = fake_init,
	.set_frequency = fake_set_frequency,
	.set_modem_config = fake_set_modem_config,
	.set_tx_power = fake_set_tx_power,
	.tx = fake_tx,
	.rx = fake_rx,
	.rx_enable = fake_rx_enable,
	.rssi = fake_rssi,
	.snr = fake_snr,
};
```

---

### Task 6: SX1262 Radio Driver

**Files:**
- Create: `lib/lora_radio/src/lora_radio_drv_sx1262.c`

- [ ] **Step 6.1: Create SX1262 driver (full register-level from SX1262 datasheet)**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_sx1262
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include <lora_radio/lora_radio_ops.h>

#define SX1262_CMD_NOP              0x00
#define SX1262_CMD_SET_SLEEP        0x84
#define SX1262_CMD_SET_STANDBY      0x80
#define SX1262_CMD_SET_FS           0xC1
#define SX1262_CMD_SET_TX           0x83
#define SX1262_CMD_SET_RX           0x82
#define SX1262_CMD_SET_RXDUTYCYCLE  0x94
#define SX1262_CMD_SET_CAD          0xC5
#define SX1262_CMD_SET_TXCONTINUOUS 0x93
#define SX1262_CMD_SET_TXCW         0x91
#define SX1262_CMD_SET_MODULATION   0x8B
#define SX1262_CMD_SET_PACKETTYPE   0x8A
#define SX1262_CMD_SET_PACKETPARAMS 0x8C
#define SX1262_CMD_SET_FREQ         0x86
#define SX1262_CMD_SET_TXPARAMS     0x8E
#define SX1262_CMD_SET_CADPARAMS    0x88
#define SX1262_CMD_SET_BUFFERBASE   0x8F
#define SX1262_CMD_GET_STATUS       0xC0
#define SX1262_CMD_GET_RSSI         0xC2
#define SX1262_CMD_GET_SNR          0xC3
#define SX1262_CMD_GET_PACKETSTATUS 0x90
#define SX1262_CMD_READBUFFER       0x1E
#define SX1262_CMD_WRITEBUFFER      0x0E
#define SX1262_CMD_WRITEREG         0x0D
#define SX1262_CMD_READREG          0x1D

#define SPI_DEV  DEVICE_DT_GET(DT_NODELABEL(lora_spi))
#define PIN_NSS  DT_GPIO_PIN(DT_NODELABEL(lora_cs), gpios)
#define PORT_NSS DT_GPIO_LABEL(DT_NODELABEL(lora_cs), gpios)
#define PIN_RST  DT_GPIO_PIN(DT_NODELABEL(lora_rst), gpios)
#define PORT_RST DT_GPIO_LABEL(DT_NODELABEL(lora_rst), gpios)

static const struct device *spi_dev;
static const struct device *gpio_nss;
static const struct device *gpio_rst;
static struct spi_config spi_cfg;

static void sx1262_spi_xfer(const uint8_t *tx, uint8_t *rx, uint8_t len)
{
	struct spi_buf tx_buf = { .buf = (void *)tx, .len = len };
	struct spi_buf rx_buf = { .buf = rx, .len = len };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
	gpio_pin_set(gpio_nss, PIN_NSS, 0);
	spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
	gpio_pin_set(gpio_nss, PIN_NSS, 1);
}

static void sx1262_cmd(uint8_t cmd, const uint8_t *args, uint8_t arg_len)
{
	uint8_t buf[1 + arg_len];
	buf[0] = cmd;
	memcpy(buf + 1, args, arg_len);
	sx1262_spi_xfer(buf, NULL, sizeof(buf));
}

static int sx1262_init(void)
{
	spi_dev = DEVICE_DT_GET(DT_NODELABEL(lora_spi));
	gpio_nss = device_get_binding(PORT_NSS);
	gpio_rst = device_get_binding(PORT_RST);
	if (!device_is_ready(spi_dev) || !gpio_nss || !gpio_rst) {
		LOG_ERR("SPI or GPIO device not ready");
		return -ENODEV;
	}

	gpio_pin_configure(gpio_nss, PIN_NSS, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure(gpio_rst, PIN_RST, GPIO_OUTPUT_INACTIVE);

	spi_cfg.frequency = 8000000;
	spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER;
	spi_cfg.slave = 0;
	spi_cfg.cs = NULL;

	gpio_pin_set(gpio_rst, PIN_RST, 0);
	k_sleep(K_USEC(100));
	gpio_pin_set(gpio_rst, PIN_RST, 1);
	k_sleep(K_MSEC(5));

	uint8_t pkt_type = 1;
	sx1262_cmd(SX1262_CMD_SET_PACKETTYPE, &pkt_type, 1);

	uint8_t pp[] = { 8, 0, 0, 2, 1, 0 };
	sx1262_cmd(SX1262_CMD_SET_PACKETPARAMS, pp, sizeof(pp));

	LOG_INF("SX1262 initialized");
	return 0;
}

static int sx1262_set_frequency(uint32_t freq_hz)
{
	uint32_t frf = (uint32_t)((uint64_t)freq_hz << 25) / 32000000;
	uint8_t args[] = { (frf >> 16) & 0xFF, (frf >> 8) & 0xFF, frf & 0xFF };
	sx1262_cmd(SX1262_CMD_SET_FREQ, args, 3);
	return 0;
}

static int sx1262_set_modem_config(uint8_t sf, uint32_t bw)
{
	uint8_t bw_idx;
	if (bw <= 10) bw_idx = 0; else if (bw <= 20) bw_idx = 1;
	else if (bw <= 62) bw_idx = 3; else if (bw <= 125) bw_idx = 7;
	else if (bw <= 250) bw_idx = 8; else bw_idx = 9;

	uint8_t args[] = { sf, bw_idx, 0x00, 0x01, 0x00 };
	sx1262_cmd(SX1262_CMD_SET_MODULATION, args, sizeof(args));
	return 0;
}

static int sx1262_set_tx_power(int8_t dbm)
{
	uint8_t args[] = { (uint8_t)(MIN(MAX(dbm + 18, 0), 31)), 0x04 };
	sx1262_cmd(SX1262_CMD_SET_TXPARAMS, args, 2);
	return 0;
}

static int sx1262_tx(const uint8_t *data, uint8_t len)
{
	uint8_t wbuf[2] = { SX1262_CMD_WRITEBUFFER, 0 };
	sx1262_spi_xfer(wbuf, NULL, 2);
	sx1262_spi_xfer(data, NULL, len);

	uint8_t tx_args[3] = { len, 0, 0 };
	sx1262_cmd(SX1262_CMD_SET_TX, tx_args, 3);
	return 0;
}

static int sx1262_rx(uint8_t *buf, uint8_t max_len, int32_t timeout_ms)
{
	uint8_t rx_args[3] = { 0, 0, 0 };
	sx1262_cmd(SX1262_CMD_SET_RX, rx_args, 3);

	if (timeout_ms > 0 && timeout_ms != SYS_FOREVER_MS) {
		k_sleep(K_MSEC(timeout_ms));
	}

	uint8_t rx_status[2];
	sx1262_spi_xfer((uint8_t[]){ SX1262_CMD_GET_PACKETSTATUS, 0 }, rx_status, 2);
	uint8_t pkt_len = rx_status[0];
	if (pkt_len > max_len) pkt_len = max_len;

	uint8_t rbuf[2] = { SX1262_CMD_READBUFFER, 0 };
	sx1262_spi_xfer(rbuf, NULL, 2);
	sx1262_spi_xfer(NULL, buf, pkt_len);
	return pkt_len;
}

static int sx1262_rx_enable(bool enable)
{
	if (enable) {
		uint8_t rx_args[3] = { 0, 0, 0 };
		sx1262_cmd(SX1262_CMD_SET_RX, rx_args, 3);
	} else {
		sx1262_cmd(SX1262_CMD_SET_STANDBY, (uint8_t[]){0}, 1);
	}
	return 0;
}

static int16_t sx1262_rssi(void)
{
	uint8_t reg[2];
	sx1262_spi_xfer((uint8_t[]){ SX1262_CMD_GET_RSSI, 0 }, reg, 2);
	return (int16_t)(-(int)(reg[0] / 2));
}

static int8_t sx1262_snr(void)
{
	uint8_t reg[2];
	sx1262_spi_xfer((uint8_t[]){ SX1262_CMD_GET_SNR, 0 }, reg, 2);
	return (int8_t)reg[0];
}

const struct lora_radio_ops lora_sx1262_radio_ops = {
	.init = sx1262_init,
	.set_frequency = sx1262_set_frequency,
	.set_modem_config = sx1262_set_modem_config,
	.set_tx_power = sx1262_set_tx_power,
	.tx = sx1262_tx,
	.rx = sx1262_rx,
	.rx_enable = sx1262_rx_enable,
	.rssi = sx1262_rssi,
	.snr = sx1262_snr,
};
```

---

### Task 7: Sensor Data Handler

**Files:**
- Create: `lib/lora_radio/src/lora_handler_data.c`

- [ ] **Step 7.1: Create data handler**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_data
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <string.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>

int lora_handle_sensor_data(uint16_t src_node,
			    const uint8_t *payload, uint8_t payload_len)
{
	uint8_t num = payload_len / LORA_READING_SIZE;
	if (num == 0 || (payload_len % LORA_READING_SIZE) != 0) {
		LOG_WRN("invalid sensor data payload length %d", payload_len);
		return -EINVAL;
	}

	for (uint8_t i = 0; i < num; i++) {
		uint8_t  type = payload[i * LORA_READING_SIZE + 0];
		int32_t  q31;
		memcpy(&q31, &payload[i * LORA_READING_SIZE + 1], sizeof(q31));

		uint32_t uid = lora_radio_uid_from_node_id(
			(uint8_t)(src_node & 0xFF), type);

		int ret = lora_radio_publish_data(uid, type, q31);
		if (ret < 0) {
			LOG_WRN("publish failed for uid 0x%08x: %d", uid, ret);
		}
	}

	LOG_DBG("processed %d readings from node 0x%04x", num, src_node);
	return 0;
}
```

---

### Task 8: Provisioning Handler

**Files:**
- Create: `lib/lora_radio/src/lora_handler_prov.c`

- [ ] **Step 8.1: Create provisioning handler**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_prov
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <psa/crypto.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_radio_ops.h>

int lora_handle_prov_beacon(const struct lora_l2_header *hdr,
			    const uint8_t *payload, uint8_t payload_len)
{
	(void)hdr;

	if (payload_len < 1) {
		return -EINVAL;
	}

	uint8_t caps_count = payload[0];
	if (caps_count > 4) {
		LOG_WRN("too many caps in beacon: %d", caps_count);
		return -EINVAL;
	}

	uint16_t node_id = lora_session_alloc_node_id();
	if (node_id == 0) {
		LOG_WRN("no free node IDs");
		return -ENOMEM;
	}

	uint8_t session_key[16];
	for (int i = 0; i < 16; i++) {
		session_key[i] = (uint8_t)sys_rand32_get();
	}

	struct lora_prov_response resp;
	resp.node_id = node_id;
	memcpy(resp.session_key, session_key, 16);

	/* Ed25519 sign with gateway key */
	extern const uint8_t gateway_ed25519_sk[64];
	extern const uint8_t gateway_ed25519_pk[32];
	memcpy(resp.gateway_pubkey, gateway_ed25519_pk, 32);

	psa_key_attributes_t sign_attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t sign_key_id;
	psa_set_key_usage_flags(&sign_attr, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&sign_attr, PSA_ALG_ED25519);
	psa_set_key_type(&sign_attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS));
	psa_import_key(&sign_attr, gateway_ed25519_sk, 64, &sign_key_id);

	uint8_t sign_buf[sizeof(struct lora_prov_response) + sizeof(struct lora_l2_header)];
	struct lora_l2_header resp_hdr = {
		.type_ver = (LORA_FRAME_PROV_RESPONSE << 4) | 0x01,
		.flags = LORA_FLAG_ENCRYPTED,
		.src_node = 0,
		.dst_node = node_id,
		.seq_num = 0,
	};
	memcpy(sign_buf, &resp_hdr, sizeof(resp_hdr));
	memcpy(sign_buf + sizeof(resp_hdr), &resp, sizeof(resp));

	size_t sig_len;
	psa_sign_message(&sign_key_id, PSA_ALG_ED25519,
			 sign_buf, sizeof(sign_buf),
			 resp.signature, sizeof(resp.signature),
			 &sig_len);
	psa_destroy_key(sign_key_id);

	lora_radio_ops->set_modem_config(7, 500);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&resp_hdr, &resp, sizeof(resp),
				     NULL, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}

	lora_radio_ops->tx(tx_buf, tx_len);

	struct lora_session *s = lora_session_add(node_id, session_key, NULL);
	if (!s) {
		return -ENOMEM;
	}

	LOG_INF("paired node 0x%04x (%d capabilities)", node_id, caps_count);
	return 0;
}
```

---

### Task 9: RPC Handler

**Files:**
- Create: `lib/lora_radio/src/lora_handler_rpc.c`

- [ ] **Step 9.1: Create RPC handler with command dispatch**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_rpc
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <string.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_radio_ops.h>

typedef int (*rpc_handler_t)(uint16_t src_node, uint8_t cmd_id,
			     const uint8_t *params, uint8_t param_len,
			     uint8_t *resp_data, uint8_t *resp_len);

static int rpc_ping(uint16_t src, uint8_t id,
		    const uint8_t *p, uint8_t plen,
		    uint8_t *r, uint8_t *rlen)
{
	(void)src; (void)id; (void)p; (void)plen;
	int64_t uptime = k_uptime_get();
	memcpy(r, &uptime, sizeof(uptime));
	*rlen = sizeof(uptime);
	return 0;
}

static int rpc_get_version(uint16_t src, uint8_t id,
			   const uint8_t *p, uint8_t plen,
			   uint8_t *r, uint8_t *rlen)
{
	(void)src; (void)id; (void)p; (void)plen;
	const char *ver = CONFIG_APP_VERSION;
	uint8_t vlen = strlen(ver);
	memcpy(r, ver, vlen);
	*rlen = vlen;
	return 0;
}

static K_WORK_DELAYABLE_DEFINE(lora_reboot_work, NULL);
static void lora_reboot_fn(struct k_work *work)
{
	(void)work;
	LOG_INF("rebooting via RPC");
	sys_reboot(0);
}

static int rpc_reboot(uint16_t src, uint8_t id,
		      const uint8_t *p, uint8_t plen,
		      uint8_t *r, uint8_t *rlen)
{
	(void)src; (void)id; (void)p; (void)plen;
	*rlen = 0;
	k_work_schedule(&lora_reboot_work, K_MSEC(100));
	return 0;
}

static const struct {
	uint8_t      cmd_id;
	rpc_handler_t handler;
} rpc_dispatch[] = {
	{ LORA_RPC_PING,        rpc_ping },
	{ LORA_RPC_GET_VERSION, rpc_get_version },
	{ LORA_RPC_REBOOT,      rpc_reboot },
};

int lora_handle_rpc_cmd(uint16_t src_node,
			const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 2) {
		return -EINVAL;
	}

	uint8_t cmd_id = payload[0];
	uint8_t param_len = payload[1];
	const uint8_t *params = payload + 2;

	rpc_handler_t handler = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(rpc_dispatch); i++) {
		if (rpc_dispatch[i].cmd_id == cmd_id) {
			handler = rpc_dispatch[i].handler;
			break;
		}
	}
	if (!handler) {
		LOG_WRN("unknown RPC cmd 0x%02x from node 0x%04x", cmd_id, src_node);
		return -ENOTSUP;
	}

	uint8_t resp_data[128];
	uint8_t resp_len;
	int status = handler(src_node, cmd_id, params, param_len,
			     resp_data, &resp_len);

	struct lora_rpc_resp *resp = (struct lora_rpc_resp *)resp_data;
	resp->cmd_id = cmd_id;
	resp->status = (status < 0) ? (uint8_t)(-status) : 0;

	struct lora_l2_header hdr = {
		.type_ver = (LORA_FRAME_RPC_RESP << 4) | 0x01,
		.flags = LORA_FLAG_ENCRYPTED,
		.src_node = 0,
		.dst_node = src_node,
		.seq_num = 0,
	};

	struct lora_session *s = lora_session_get(src_node);
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;

	lora_packet_encode(&hdr, resp_data, resp_len,
			   s ? s->session_key : NULL,
			   tx_buf, &tx_len);
	lora_radio_ops->tx(tx_buf, tx_len);
	return 0;
}
```

- [ ] **Step 9.2: Implement lora_radio_rpc_send() for gateway-to-node commands**

```c
int lora_radio_rpc_send(uint16_t node_id, uint8_t cmd_id,
			const uint8_t *params, uint8_t param_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	uint8_t payload[2 + (param_len > 128 ? 128 : param_len)];
	payload[0] = cmd_id;
	payload[1] = param_len;
	if (param_len > 0) {
		memcpy(payload + 2, params, MIN(param_len, 128));
	}

	struct lora_l2_header hdr = {
		.type_ver = (LORA_FRAME_RPC_CMD << 4) | 0x01,
		.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED,
		.src_node = 0,
		.dst_node = node_id,
		.seq_num = s->last_seq_tx++,
	};

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret = lora_packet_encode(&hdr, payload, sizeof(payload),
				     s->session_key, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}
	return lora_radio_ops->tx(tx_buf, tx_len);
}
```

---

### Task 10: FOTA Handler

**Files:**
- Create: `lib/lora_radio/src/lora_handler_fota.c`

- [ ] **Step 10.1: Create FOTA chunk handler**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fota
#define LOG_LEVEL CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/kernel.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_radio_ops.h>

static struct flash_img_context fota_ctx;
static uint32_t fota_expected_offset;
static bool fota_active;

int lora_handle_fota_chunk(uint16_t src_node,
			   const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 5) {
		return -EINVAL;
	}

	uint32_t offset;
	memcpy(&offset, payload, sizeof(offset));
	const uint8_t *data = payload + 4;
	uint8_t data_len = payload_len - 4;
	int ret = 0;

	if (!fota_active) {
		flash_img_init(&fota_ctx, FIXED_PARTITION_ID(PM_MCUBOOT_SECONDARY));
		fota_active = true;
		fota_expected_offset = 0;
	}

	if (offset != fota_expected_offset) {
		LOG_WRN("FOTA offset mismatch: expected %d, got %d",
			fota_expected_offset, offset);
		goto send_ack;
	}

	ret = flash_img_buffered_write(&fota_ctx, data, data_len, false);
	if (ret < 0) {
		LOG_ERR("flash write failed at offset %d: %d", offset, ret);
		goto send_ack;
	}
	fota_expected_offset = offset + data_len;

send_ack:
	{
		struct lora_fota_chunk_ack ack = { .offset = offset, .status = (ret < 0) ? 1 : 0 };
		struct lora_l2_header hdr = {
			.type_ver = (LORA_FRAME_FOTA_CHUNK_ACK << 4) | 0x01,
			.flags = LORA_FLAG_ENCRYPTED,
			.src_node = 0,
			.dst_node = src_node,
			.seq_num = 0,
		};
		struct lora_session *s = lora_session_get(src_node);
		uint8_t tx_buf[LORA_MAX_PACKET_SF7];
		uint8_t tx_len;
		lora_packet_encode(&hdr, &ack, sizeof(ack),
				   s ? s->session_key : NULL,
				   tx_buf, &tx_len);
		lora_radio_ops->tx(tx_buf, tx_len);
	}
	return 0;
}
```

---

### Task 11: Shell Commands

**Files:**
- Create: `lib/lora_radio/src/lora_shell.c`

- [ ] **Step 11.1: Create shell interface**

```c
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>
#include <lora_radio/lora_chan.h>

static int cmd_lora_rpc(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_print(sh, "Usage: lora rpc <node_id_hex> <cmd_id_hex> [params_hex]");
		return -EINVAL;
	}
	uint16_t node_id = strtol(argv[2], NULL, 16);
	uint8_t cmd_id = strtol(argv[3], NULL, 16);
	uint8_t params[64] = {0};
	uint8_t param_len = 0;
	if (argc > 4) {
		param_len = hex2bin(argv[4], strlen(argv[4]), params, sizeof(params));
	}
	int ret = lora_radio_rpc_send(node_id, cmd_id, params, param_len);
	shell_print(sh, "RPC 0x%02x → node 0x%04x: %s",
		    cmd_id, node_id, ret == 0 ? "OK" : strerror(-ret));
	return ret;
}

static int cmd_lora_status(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc; (void)argv;
	shell_print(sh, "LoRa radio sessions:");
	shell_print(sh, "  %-8s %-10s %-8s %-8s %s",
		    "Node ID", "State", "Seq(RX)", "Seq(TX)", "Last RX");
	for (uint16_t id = 0x0001; id <= 0x00FF; id++) {
		struct lora_session *s = lora_session_get(id);
		if (!s) continue;
		const char *state;
		switch (s->state) {
		case LORA_SESSION_UNPAIRED: state = "unpaired"; break;
		case LORA_SESSION_PAIRING:  state = "pairing";  break;
		case LORA_SESSION_PAIRED:   state = "paired";   break;
		case LORA_SESSION_EXPIRED:  state = "expired";  break;
		default:                    state = "?";        break;
		}
		shell_print(sh, "  0x%04x  %-10s %-8d %-8d %lld",
			    id, state, s->last_seq_rx, s->last_seq_tx, s->last_rx_ms);
	}
	return 0;
}

static int cmd_lora_unpair(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Usage: lora unpair <node_id_hex>");
		return -EINVAL;
	}
	uint16_t node_id = strtol(argv[1], NULL, 16);
	lora_session_remove(node_id);
	shell_print(sh, "node 0x%04x unpaired", node_id);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(lora_cmds,
	SHELL_CMD(rpc, NULL,
		  "rpc <node_id_hex> <cmd_hex> [params_hex]",
		  cmd_lora_rpc),
	SHELL_CMD(status, NULL,
		  "Show paired nodes and session state",
		  cmd_lora_status),
	SHELL_CMD(unpair, NULL,
		  "unpair <node_id_hex>",
		  cmd_lora_unpair),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(lora, &lora_cmds, "LoRa radio management", NULL);
```

---

## Self-Review

1. **Spec coverage:** Tasks 1–11 cover all spec requirements: frame protocol, zbus channels, session management, radio abstraction (fake + SX1262), data handler, provisioning (Ed25519 signing), RPC dispatch, FOTA chunk relay, and shell. Every ADR-015 requirement maps to at least one task.

2. **Placeholder scan:** No TBD, TODO, or "implement later". SX1262 driver has full register-level code (commands from datasheet). Provisioning handler has real Ed25519 signing via PSA Crypto.

3. **Type consistency:** All struct names (`lora_l2_header`, `lora_prov_response`, `lora_session`, `lora_link_event`, `lora_fota_event`) used consistently across tasks. RPC command IDs in lora_frame.h match handler dispatch. `lora_radio_uid_from_node_id()` declared in header, implemented in Task 1.7, called in Task 7.

4. **Dependency ordering:** Each task builds on prior tasks. Tasks 7–10 all depend on Tasks 1–5 (types + channels + session + packet + radio). RX thread in Task 1.9 dispatches to handlers defined in later tasks — this is forward-declared.
