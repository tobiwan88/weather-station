/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/remote_sensor_uid)
 * @brief Unit tests for remote sensor UID derivation helpers.
 *
 * UID bit layout (from remote_sensor.h):
 *   bits [31:16]  protocol prefix   (passed as prefix argument)
 *   bits [15: 4]  device hash       (CRC12 of addr, or node_id for LoRa)
 *   bits [ 3: 0]  sensor_type slot  (enum sensor_type value)
 */

#include <zephyr/ztest.h>

#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>

ZTEST_SUITE(remote_sensor_uid_suite, NULL, NULL, NULL, NULL, NULL);

/* ── remote_sensor_uid_from_addr ─────────────────────────────────────────── */

/**
 * @brief Prefix must occupy bits [31:16] exactly.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_addr_prefix_bits)
{
	static const uint8_t addr[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	const uint16_t prefix = 0x0100U;

	uint32_t uid = remote_sensor_uid_from_addr(prefix, addr, sizeof(addr),
						   SENSOR_TYPE_TEMPERATURE);

	zassert_equal((uid >> 16) & 0xFFFFU, prefix,
		      "prefix bits [31:16] mismatch: uid=0x%08x prefix=0x%04x",
		      uid, prefix);
}

/**
 * @brief Type slot must occupy bits [3:0] exactly.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_addr_type_slot_bits)
{
	static const uint8_t addr[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
	const uint16_t prefix = 0x0100U;

	uint32_t uid_temp = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_hum = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_HUMIDITY);

	zassert_equal(uid_temp & 0x0FU, (uint32_t)SENSOR_TYPE_TEMPERATURE,
		      "type slot for TEMPERATURE wrong: uid=0x%08x", uid_temp);
	zassert_equal(uid_hum & 0x0FU, (uint32_t)SENSOR_TYPE_HUMIDITY,
		      "type slot for HUMIDITY wrong: uid=0x%08x", uid_hum);
}

/**
 * @brief Same address + different sensor types → different UIDs.
 *
 * This is the "one UID per (device × type)" invariant.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_addr_different_types_differ)
{
	static const uint8_t addr[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	const uint16_t prefix = 0x0100U;

	uint32_t uid_temp = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_hum = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_HUMIDITY);
	uint32_t uid_pres = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_PRESSURE);

	zassert_not_equal(uid_temp, uid_hum,
			  "TEMPERATURE and HUMIDITY UIDs must differ for same addr");
	zassert_not_equal(uid_temp, uid_pres,
			  "TEMPERATURE and PRESSURE UIDs must differ for same addr");
	zassert_not_equal(uid_hum, uid_pres,
			  "HUMIDITY and PRESSURE UIDs must differ for same addr");
}

/**
 * @brief Same address + same type → identical UIDs (stability across calls).
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_addr_stable)
{
	static const uint8_t addr[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
	const uint16_t prefix = 0x0100U;

	uint32_t uid_a = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_b = remote_sensor_uid_from_addr(
		prefix, addr, sizeof(addr), SENSOR_TYPE_TEMPERATURE);

	zassert_equal(uid_a, uid_b,
		      "Same addr+type must produce identical UIDs: "
		      "0x%08x != 0x%08x", uid_a, uid_b);
}

/**
 * @brief Different addresses → different device hash bits [15:4].
 *
 * Checks that distinct hardware addresses are not collapsed to the same slot.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_addr_different_addrs_differ)
{
	static const uint8_t addr_a[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	static const uint8_t addr_b[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
	const uint16_t prefix = 0x0100U;

	uint32_t uid_a = remote_sensor_uid_from_addr(
		prefix, addr_a, sizeof(addr_a), SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_b = remote_sensor_uid_from_addr(
		prefix, addr_b, sizeof(addr_b), SENSOR_TYPE_TEMPERATURE);

	zassert_not_equal(uid_a, uid_b,
			  "Adjacent addresses must produce distinct UIDs: "
			  "both 0x%08x", uid_a);
}

/* ── remote_sensor_uid_from_node_id ─────────────────────────────────────── */

/**
 * @brief Prefix must occupy bits [31:16] for node_id path too.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_node_id_prefix_bits)
{
	const uint16_t prefix = 0x0200U;

	uint32_t uid = remote_sensor_uid_from_node_id(
		prefix, 0x05, SENSOR_TYPE_TEMPERATURE);

	zassert_equal((uid >> 16) & 0xFFFFU, prefix,
		      "prefix bits [31:16] mismatch: uid=0x%08x", uid);
}

/**
 * @brief node_id must occupy bits [11:4] (i.e. (node_id << 4)).
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_node_id_node_bits)
{
	const uint16_t prefix = 0x0200U;
	const uint8_t node_id = 0x07U;

	uint32_t uid = remote_sensor_uid_from_node_id(
		prefix, node_id, SENSOR_TYPE_TEMPERATURE);

	uint8_t extracted = (uint8_t)((uid >> 4) & 0xFFU);

	zassert_equal(extracted, node_id,
		      "node_id bits [11:4] wrong: uid=0x%08x node_id=0x%02x "
		      "extracted=0x%02x", uid, node_id, extracted);
}

/**
 * @brief Deterministic: same node_id + type always produces the same UID.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_node_id_deterministic)
{
	const uint16_t prefix = 0x0200U;

	for (int node = 0; node < 8; node++) {
		uint32_t uid_a = remote_sensor_uid_from_node_id(
			prefix, (uint8_t)node, SENSOR_TYPE_TEMPERATURE);
		uint32_t uid_b = remote_sensor_uid_from_node_id(
			prefix, (uint8_t)node, SENSOR_TYPE_TEMPERATURE);

		zassert_equal(uid_a, uid_b,
			      "node_id %d: UIDs differ across calls: "
			      "0x%08x != 0x%08x", node, uid_a, uid_b);
	}
}

/**
 * @brief Different node_ids must produce different UIDs (same type).
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_node_id_different_nodes_differ)
{
	const uint16_t prefix = 0x0200U;

	uint32_t uid_0 = remote_sensor_uid_from_node_id(
		prefix, 0, SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_1 = remote_sensor_uid_from_node_id(
		prefix, 1, SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_2 = remote_sensor_uid_from_node_id(
		prefix, 2, SENSOR_TYPE_TEMPERATURE);

	zassert_not_equal(uid_0, uid_1, "node 0 and 1 must have different UIDs");
	zassert_not_equal(uid_0, uid_2, "node 0 and 2 must have different UIDs");
	zassert_not_equal(uid_1, uid_2, "node 1 and 2 must have different UIDs");
}

/**
 * @brief Same node_id but different types → different UIDs.
 */
ZTEST(remote_sensor_uid_suite, test_uid_from_node_id_different_types_differ)
{
	const uint16_t prefix = 0x0200U;

	uint32_t uid_temp = remote_sensor_uid_from_node_id(
		prefix, 5, SENSOR_TYPE_TEMPERATURE);
	uint32_t uid_hum = remote_sensor_uid_from_node_id(
		prefix, 5, SENSOR_TYPE_HUMIDITY);

	zassert_not_equal(uid_temp, uid_hum,
			  "Same node_id with different types must produce "
			  "different UIDs");
}

/* ── Cross-protocol isolation ────────────────────────────────────────────── */

/**
 * @brief BLE and LoRa UIDs must not overlap for the same type slot.
 *
 * Exercises the prefix-isolation property: different protocol prefixes must
 * yield UIDs in non-overlapping 16-bit ranges within the 32-bit space.
 */
ZTEST(remote_sensor_uid_suite, test_uid_no_cross_protocol_collision)
{
	static const uint8_t ble_addr[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
	const uint16_t ble_prefix  = 0x0100U;
	const uint16_t lora_prefix = 0x0200U;

	/* Construct a LoRa UID whose node_id maps to the same lower bits as
	 * a BLE CRC12 result — both for TEMPERATURE type. */
	uint32_t ble_uid  = remote_sensor_uid_from_addr(
		ble_prefix, ble_addr, sizeof(ble_addr), SENSOR_TYPE_TEMPERATURE);
	uint32_t lora_uid = remote_sensor_uid_from_node_id(
		lora_prefix, 0x00, SENSOR_TYPE_TEMPERATURE);

	zassert_not_equal(ble_uid, lora_uid,
			  "BLE (0x%08x) and LoRa (0x%08x) UIDs collide — "
			  "prefix isolation is broken", ble_uid, lora_uid);

	/* Verify the high bytes differ. */
	zassert_not_equal((ble_uid >> 16) & 0xFFFFU,
			  (lora_uid >> 16) & 0xFFFFU,
			  "BLE and LoRa prefix bits must differ");
}
