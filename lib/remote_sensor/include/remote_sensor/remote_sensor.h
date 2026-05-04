/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file remote_sensor.h
 * @brief Transport-agnostic abstraction for wirelessly-connected remote sensors.
 *
 * Architecture:
 *
 *   Protocol adapters (lib/ble_sensor/, lib/lora_sensor/, …) implement the
 *   remote_transport vtable and declare one instance per protocol via
 *   REMOTE_TRANSPORT_DEFINE().  The remote_sensor_manager (a listener+workqueue
 *   dispatch) iterates all linked-in transports, dispatches scan control
 *   commands, processes discovery events, registers discovered sensors in
 *   sensor_registry, and routes trigger events to capable transports.
 *
 *   Data from remote sensors reaches sensor_event_chan via
 *   remote_sensor_publish_data() — identical to local sensors from the
 *   perspective of all consumers (dashboard, display, logger).
 *
 * Discovery delivery:
 *   remote_sensor_announce_disc() — zbus-backed helper for transports
 *   remote_scan_ctrl_chan         — ZBUS_CHAN_DEFINE in remote_scan_ctrl_chan.c
 *
 * UID scheme (one UID per physical-device × sensor_type pair):
 *   BLE/Thread: uid = (prefix << 16) | (crc12(addr) << 4) | type_slot
 *   LoRa:       uid = (prefix << 16) | (node_id    << 4) | type_slot
 *   type_slot   = (uint8_t)sensor_type  (must fit in 4 bits → max 15 types)
 */

#ifndef REMOTE_SENSOR_REMOTE_SENSOR_H_
#define REMOTE_SENSOR_REMOTE_SENSOR_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Protocol identity enum
 * --------------------------------------------------------------------------
 * Values are stored in persistent settings ("rsen/<uid>/proto") — they must
 * never be renumbered.  Add new protocols at the end.
 */

/**
 * @brief Identifies the wireless protocol a transport adapter implements.
 *
 * Used in the vtable, discovery/scan-control events, and settings persistence.
 * Enables switch-based dispatch without string comparisons.
 *
 * REMOTE_TRANSPORT_PROTO_UNKNOWN acts as a wildcard in scan-control events
 * (proto == UNKNOWN means "apply to all transports").
 */
enum remote_transport_proto {
	REMOTE_TRANSPORT_PROTO_UNKNOWN = 0, /**< Wildcard / unspecified         */
	REMOTE_TRANSPORT_PROTO_BLE = 1,     /**< Bluetooth Low Energy           */
	REMOTE_TRANSPORT_PROTO_LORA = 2,    /**< LoRa / LoRaWAN                 */
	REMOTE_TRANSPORT_PROTO_THREAD = 3,  /**< Thread (IEEE 802.15.4)         */
	REMOTE_TRANSPORT_PROTO_PIPE = 4,    /**< POSIX FIFO (native_sim testing) */
	REMOTE_TRANSPORT_PROTO_FAKE = 15,   /**< Simulated stub (testing only)  */
};

/* --------------------------------------------------------------------------
 * Transport capability flags
 * -------------------------------------------------------------------------- */

/** Transport can actively scan for / discover new peers. */
#define REMOTE_TRANSPORT_CAP_SCAN BIT(0)
/** Transport can forward a sensor_trigger_event to a remote peer (pull). */
#define REMOTE_TRANSPORT_CAP_TRIGGER BIT(1)

/* --------------------------------------------------------------------------
 * Transport vtable
 * -------------------------------------------------------------------------- */

/**
 * @brief Maximum number of opaque bytes needed to address any supported peer.
 *
 * BLE = 6 (48-bit MAC), LoRa = 1 (node_id byte), Thread EUI-64 = 8.
 */
#define REMOTE_SENSOR_ADDR_MAX_LEN 8

/**
 * @brief Protocol adapter vtable.
 *
 * Declare exactly one instance per protocol adapter using
 * REMOTE_TRANSPORT_DEFINE().  The remote_sensor_manager locates all
 * registered transports at link time via STRUCT_SECTION_FOREACH().
 *
 * All callbacks are invoked from the remote_sensor_manager thread.
 * They may block but MUST NOT call zbus_chan_pub() on remote_discovery_chan
 * or remote_scan_ctrl_chan directly — use remote_sensor_publish_data() for
 * measurement data and zbus_chan_pub(&remote_discovery_chan, …) for discovery.
 */
struct remote_transport {
	/** Human-readable name for logs and shell output ("ble", "lora", …). */
	const char *name;

	/** Protocol identity used in switch statements and persistence. */
	enum remote_transport_proto proto;

	/** Bitmask of REMOTE_TRANSPORT_CAP_* flags. */
	uint32_t caps;

	/**
	 * @brief Begin scanning for nearby sensor peers.
	 *
	 * Called by the manager on boot (if CONFIG_REMOTE_SENSOR_AUTO_SCAN=y)
	 * or when a REMOTE_SCAN_START event arrives for this proto (or
	 * REMOTE_TRANSPORT_PROTO_UNKNOWN = all).
	 *
	 * @return 0 on success, negative errno on failure.
	 */
	int (*scan_start)(const struct remote_transport *t);

	/**
	 * @brief Stop scanning.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*scan_stop)(const struct remote_transport *t);

	/**
	 * @brief Add a peer (begin connection / subscription).
	 *
	 * Called by the manager after registering a newly discovered sensor.
	 * The peer_addr bytes are opaque and protocol-specific.
	 *
	 * @param peer_addr Protocol-specific address (BLE MAC, LoRa node_id, …).
	 * @param addr_len  Number of valid bytes in peer_addr.
	 * @param uid       The UID assigned to this peer in sensor_registry.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*peer_add)(const struct remote_transport *t, const uint8_t *peer_addr, size_t addr_len,
			uint32_t uid);

	/**
	 * @brief Remove a peer (disconnect / unsubscribe).
	 * @param uid Previously assigned sensor UID.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*peer_remove)(const struct remote_transport *t, uint32_t uid);

	/**
	 * @brief Forward a trigger to a remote sensor (pull protocols only).
	 *
	 * Only called when REMOTE_TRANSPORT_CAP_TRIGGER is set.
	 * Set to NULL for push-only protocols (LoRa).
	 * target_uid is never 0 here — the manager broadcasts to each uid.
	 *
	 * @param uid Target sensor UID.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*send_trigger)(const struct remote_transport *t, uint32_t uid);

	/** Optional opaque private data for the adapter. */
	void *user_data;
};

/**
 * @brief Declare a transport vtable in the iterable linker section.
 *
 * Place this macro in exactly one .c file per protocol adapter.
 *
 * Example:
 * @code
 *   REMOTE_TRANSPORT_DEFINE(ble_sensor_transport, {
 *       .name        = "ble",
 *       .proto       = REMOTE_TRANSPORT_PROTO_BLE,
 *       .caps        = REMOTE_TRANSPORT_CAP_SCAN | REMOTE_TRANSPORT_CAP_TRIGGER,
 *       .scan_start  = ble_scan_start,
 *       .scan_stop   = ble_scan_stop,
 *       .peer_add    = ble_peer_add,
 *       .peer_remove = ble_peer_remove,
 *       .send_trigger = ble_send_trigger,
 *   });
 * @endcode
 */
#define REMOTE_TRANSPORT_DEFINE(inst, ...)                                                         \
	STRUCT_SECTION_ITERABLE(remote_transport, inst) = __VA_ARGS__

/* --------------------------------------------------------------------------
 * Discovery event (remote_discovery_chan)
 * -------------------------------------------------------------------------- */

/** Action carried by a remote_discovery_event. */
enum remote_discovery_action {
	REMOTE_DISCOVERY_FOUND, /**< New sensor peer detected during scan    */
	REMOTE_DISCOVERY_LOST,  /**< Known peer went out of range / offline  */
};

/**
 * @brief Announces that a remote sensor has been found or lost.
 *
 * Published on remote_discovery_chan by transport adapters.
 * The remote_sensor_manager subscribes and decides whether to
 * register/deregister the sensor.
 *
 * One FOUND event per (physical device × sensor_type) pair.
 * A dual-type BLE node (temperature + humidity) emits two FOUND events
 * with different sensor_type and suggested_uid values.
 *
 * Must be a flat, pointer-free struct (zbus rule).
 */
struct remote_discovery_event {
	/** Whether the sensor was found or lost. */
	enum remote_discovery_action action;

	/** Protocol that produced this event. */
	enum remote_transport_proto proto;

	/**
	 * Human-readable label suggested by the transport (e.g., BLE device
	 * name from advertising data).  The manager pre-seeds
	 * sensor_registry_meta.display_name with this value.
	 * Empty string if unavailable.
	 */
	char suggested_label[CONFIG_SENSOR_REGISTRY_META_NAME_LEN + 1];

	/**
	 * Protocol-specific peer address (BLE 6-byte MAC, LoRa 1-byte node_id,
	 * Thread 8-byte EUI-64).  Opaque to the manager; stored alongside the
	 * UID for future peer_add() calls after reboots.
	 */
	uint8_t peer_addr[REMOTE_SENSOR_ADDR_MAX_LEN];

	/** Number of valid bytes in peer_addr. */
	uint8_t peer_addr_len;

	/** Physical quantity this event describes. */
	enum sensor_type sensor_type;

	/**
	 * UID pre-computed by the adapter using remote_sensor_uid_from_addr()
	 * or remote_sensor_uid_from_node_id().
	 * The manager validates uniqueness before accepting.
	 */
	uint32_t suggested_uid;
};

/**
 * @brief Publish a discovery event on remote_discovery_chan.
 *
 * Transport adapters MUST use this function rather than posting to the channel
 * directly.  Publishes on remote_discovery_chan via zbus_chan_pub().
 *
 * May be called from any thread context.
 *
 * @param evt Discovery event to publish (copied by value by zbus).
 * @return 0 on success, negative errno from zbus_chan_pub() on failure.
 */
int remote_sensor_announce_disc(const struct remote_discovery_event *evt);

/* --------------------------------------------------------------------------
 * Scan control event (remote_scan_ctrl_chan)
 * -------------------------------------------------------------------------- */

/** Action to perform on the target transport(s). */
enum remote_scan_ctrl_action {
	REMOTE_SCAN_START, /**< Begin scanning for new peers  */
	REMOTE_SCAN_STOP,  /**< Stop ongoing scan             */
};

/**
 * @brief Command to start or stop scanning on one or all transports.
 *
 * Published on remote_scan_ctrl_chan by:
 *   - remote_sensor_manager (at boot, if CONFIG_REMOTE_SENSOR_AUTO_SCAN=y)
 *   - Shell command: "remote_sensor scan start [ble|lora|thread]"
 *   - Future MQTT command handler
 *
 * Must be a flat, pointer-free struct (zbus rule).
 */
struct remote_scan_ctrl_event {
	/** Action to perform. */
	enum remote_scan_ctrl_action action;

	/**
	 * Target transport protocol.
	 * REMOTE_TRANSPORT_PROTO_UNKNOWN = broadcast to all transports.
	 */
	enum remote_transport_proto proto;
};

/** zbus channel carrying remote_scan_ctrl_event (defined in remote_scan_ctrl_chan.c). */
ZBUS_CHAN_DECLARE(remote_scan_ctrl_chan);

/** zbus channel carrying remote_discovery_event (defined in remote_discovery_chan.c). */
ZBUS_CHAN_DECLARE(remote_discovery_chan);

/* --------------------------------------------------------------------------
 * Public helpers called by transport adapters
 * -------------------------------------------------------------------------- */

/**
 * @brief Publish a measurement received from a remote sensor.
 *
 * Builds an env_sensor_data event and publishes it to sensor_event_chan
 * (K_NO_WAIT).  The timestamp is assigned here using k_uptime_get() so
 * it matches the local clock — identical to local sensor events from the
 * perspective of all consumers.
 *
 * May be called from any thread context.  The uid must be registered in
 * sensor_registry (enforced with LOG_WRN in debug builds).
 *
 * Q31 encoding is the caller's responsibility — use the helpers in
 * sensor_event.h (temperature_c_to_q31, humidity_pct_to_q31, …).
 *
 * @param uid       Sensor UID as assigned at registration time.
 * @param type      Physical quantity.
 * @param q31_value Already Q31-encoded measurement value.
 * @return 0 on success, negative errno from zbus_chan_pub on failure.
 */
int remote_sensor_publish_data(uint32_t uid, enum sensor_type type, int32_t q31_value);

/**
 * @brief Derive a stable UID from a variable-length hardware address.
 *
 * Intended for BLE (6-byte MAC) and Thread (8-byte EUI-64) adapters.
 *
 * uid = (prefix << 16) | (crc12(addr, len) << 4) | (uint8_t)type
 *
 * The 12-bit CRC provides 4096 distinct device slots per protocol prefix.
 * The lower 4 bits encode sensor_type, so one physical device with N types
 * produces N distinct UIDs from the same address.
 *
 * @param prefix  16-bit protocol prefix (from Kconfig, e.g. 0x0100 for BLE).
 * @param addr    Protocol-specific hardware address bytes.
 * @param len     Number of bytes in addr (max REMOTE_SENSOR_ADDR_MAX_LEN).
 * @param type    Sensor type for the lower 4 bits of the UID.
 * @return Derived 32-bit UID.
 */
uint32_t remote_sensor_uid_from_addr(uint16_t prefix, const uint8_t *addr, size_t len,
				     enum sensor_type type);

/**
 * @brief Derive a stable UID from a LoRa node_id byte.
 *
 * Deterministic mapping (no CRC) because the 1-byte node_id space is small
 * and CRC would be a trivially bijective identity.
 *
 * uid = (prefix << 16) | (node_id << 4) | (uint8_t)type
 *
 * Supports up to 256 LoRa nodes × 16 sensor types = 4096 LoRa UIDs
 * within each prefix range.
 *
 * @param prefix   16-bit protocol prefix (e.g. 0x0200 for LoRa).
 * @param node_id  LoRa node identifier (0–255).
 * @param type     Sensor type for the lower 4 bits of the UID.
 * @return Derived 32-bit UID.
 */
uint32_t remote_sensor_uid_from_node_id(uint16_t prefix, uint8_t node_id, enum sensor_type type);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_SENSOR_REMOTE_SENSOR_H_ */
