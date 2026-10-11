/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/lora_session)
 * @brief Unit tests for LoRa session lifecycle.
 */

#include <lora_radio/lora_session.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(lora_session_suite, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Session add → get → remove lifecycle.
 */
ZTEST(lora_session_suite, test_add_get_remove)
{
	uint8_t key[16] = {0};

	lora_session_init();

	struct lora_session *s = lora_session_add(1, key);
	zassert_not_null(s, "add should return session");
	zassert_equal(s->node_id, 1, "node_id mismatch");
	zassert_equal(s->state, LORA_SESSION_PAIRED, "state should be PAIRED");

	s = lora_session_get(1);
	zassert_not_null(s, "get should return session");

	lora_session_remove(1);
	s = lora_session_get(1);
	zassert_is_null(s, "get should return NULL after remove");
}

/**
 * @brief Get nonexistent session returns NULL.
 */
ZTEST(lora_session_suite, test_get_nonexistent)
{
	lora_session_init();

	struct lora_session *s = lora_session_get(99);
	zassert_is_null(s, "get nonexistent should return NULL");
}

/**
 * @brief Duplicate add creates a second entry (no duplicate check).
 */
ZTEST(lora_session_suite, test_duplicate_add_creates_second_entry)
{
	uint8_t key[16] = {0};

	lora_session_init();

	struct lora_session *s1 = lora_session_add(2, key);
	zassert_not_null(s1, "first add should succeed");

	/* lora_session_add does NOT check for duplicates — it appends. */
	struct lora_session *s2 = lora_session_add(2, key);
	zassert_not_null(s2, "second add should also succeed");
	zassert_not_equal(s1, s2, "duplicate add should return different slot");

	/* Both entries can be retrieved (get returns the first match). */
	struct lora_session *g = lora_session_get(2);
	zassert_equal(g, s1, "get should return first matching entry");

	lora_session_remove(2);
	/* After remove, the second entry is memmoved to slot 0.
	 * The pointer s2 still points to the old slot 1 address, but
	 * lora_session_get returns the new slot 0 address. */
	g = lora_session_get(2);
	zassert_not_null(g, "get should return remaining entry");
	zassert_equal(g->node_id, 2, "remaining entry should have node_id 2");

	lora_session_remove(2);
	g = lora_session_get(2);
	zassert_is_null(g, "get should return NULL after both removed");
}

/**
 * @brief Node ID allocation returns first unused ID.
 */
ZTEST(lora_session_suite, test_node_id_allocation)
{
	lora_session_init();

	/* With no sessions, alloc returns NODE_ID_MIN (0x0001). */
	uint16_t id1 = lora_session_alloc_node_id();
	zassert_equal(id1, 0x0001, "first alloc should return 0x0001");

	/* Add a session with node_id 0x0001, then alloc should return 0x0002. */
	uint8_t key[16] = {0};
	lora_session_add(0x0001, key);

	uint16_t id2 = lora_session_alloc_node_id();
	zassert_equal(id2, 0x0002, "second alloc should return 0x0002");

	lora_session_remove(0x0001);
}

/**
 * @brief Session struct size is reasonable (no accidental bloat).
 */
ZTEST(lora_session_suite, test_session_struct_size)
{
	/* 2 + 4(pad) + 16 + 2 + 2 + 1 + 4(pad) + 8 + 2 + 130 + 1 + 3(pad) = 176 */
	zassert_equal(sizeof(struct lora_session), 176, "session size is %zu, expected 176",
		      sizeof(struct lora_session));
}
