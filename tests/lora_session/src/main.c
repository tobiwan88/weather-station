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
 * @brief Removing a middle entry keeps the others; removing the last entry
 * must not memmove (guard) and removing from an empty table is a no-op.
 */
ZTEST(lora_session_suite, test_remove_memmove_guard)
{
	uint8_t key[16] = {0};

	lora_session_init();
	lora_session_remove(1); /* empty table */

	zassert_not_null(lora_session_add(1, key));
	zassert_not_null(lora_session_add(2, key));
	zassert_not_null(lora_session_add(3, key));

	lora_session_remove(2); /* middle: 3 shifts down */
	zassert_is_null(lora_session_get(2));
	zassert_not_null(lora_session_get(1));
	zassert_not_null(lora_session_get(3));

	lora_session_remove(3); /* last entry: no shift */
	zassert_is_null(lora_session_get(3));
	zassert_not_null(lora_session_get(1));

	lora_session_remove(42); /* unknown id */
	zassert_not_null(lora_session_get(1));
}

/**
 * @brief Session table full returns NULL.
 */
ZTEST(lora_session_suite, test_table_full)
{
	uint8_t key[16] = {0};

	lora_session_init();
	for (uint16_t i = 1; i <= 4; i++) {
		zassert_not_null(lora_session_add(i, key));
	}
	zassert_is_null(lora_session_add(5, key));
}

/**
 * @brief Replay protection: first frame accepted, equal/lower dropped, higher accepted.
 */
ZTEST(lora_session_suite, test_replay_drop)
{
	uint8_t key[16] = {0};

	lora_session_init();
	struct lora_session *s = lora_session_add(1, key);

	zassert_true(lora_session_rx_accept(s, 100), "first frame accepted");
	zassert_false(lora_session_rx_accept(s, 100), "equal seq dropped");
	zassert_false(lora_session_rx_accept(s, 99), "lower seq dropped");
	zassert_equal(s->replay_drops, 2);
	zassert_true(lora_session_rx_accept(s, 101), "higher seq accepted");
	zassert_equal(s->last_seq_rx, 101);

	/* Another node is independent */
	struct lora_session *s2 = lora_session_add(2, key);

	zassert_true(lora_session_rx_accept(s2, 5));
}

/**
 * @brief The very first frame is accepted even with seq 0 (reboot case).
 */
ZTEST(lora_session_suite, test_first_frame_seq_zero)
{
	uint8_t key[16] = {0};

	lora_session_init();
	struct lora_session *s = lora_session_add(1, key);

	zassert_true(lora_session_rx_accept(s, 0));
	zassert_false(lora_session_rx_accept(s, 0));
}

static uint32_t persisted;
static int persist_calls;
static int persist_rc;

static int fake_persist(uint32_t reserved)
{
	persist_calls++;
	if (persist_rc == 0) {
		persisted = reserved;
	}
	return persist_rc;
}

/**
 * @brief Counter reservation: one persist per block, never reuse after reboot.
 */
ZTEST(lora_session_suite, test_counter_reservation)
{
	struct lora_seq_counter c;
	uint32_t seq;

	persist_calls = 0;
	persist_rc = 0;
	persisted = 0;
	lora_seq_counter_init(&c, 0);

	for (uint32_t i = 1; i <= 64; i++) {
		zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
		zassert_equal(seq, i);
	}
	zassert_equal(persist_calls, 1, "one write for 64 values");
	zassert_equal(persisted, 65);

	zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
	zassert_equal(seq, 65);
	zassert_equal(persist_calls, 2);
	zassert_equal(persisted, 129);

	/* "Reboot": resume from the persisted bound, no value below it reused */
	lora_seq_counter_init(&c, persisted);
	zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
	zassert_true(seq >= 66, "seq %u reused", seq);
	zassert_equal(seq, 129);
}

/**
 * @brief A failed persist hands out no value and does not advance the counter.
 */
ZTEST(lora_session_suite, test_counter_persist_failure)
{
	struct lora_seq_counter c;
	uint32_t seq = 0;

	persist_calls = 0;
	persist_rc = -EIO;
	lora_seq_counter_init(&c, 0);

	zassert_equal(lora_seq_counter_next(&c, 64, fake_persist, &seq), -EIO);
	zassert_equal(seq, 0);

	persist_rc = 0;
	zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
	zassert_equal(seq, 1);
}

/**
 * @brief The counter never wraps.
 */
ZTEST(lora_session_suite, test_counter_overflow)
{
	struct lora_seq_counter c;
	uint32_t seq;

	persist_rc = 0;
	lora_seq_counter_init(&c, UINT32_MAX - 2);

	zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
	zassert_equal(seq, UINT32_MAX - 2);
	zassert_ok(lora_seq_counter_next(&c, 64, fake_persist, &seq));
	zassert_equal(seq, UINT32_MAX - 1);
	zassert_equal(lora_seq_counter_next(&c, 64, fake_persist, &seq), -EOVERFLOW);
}

/**
 * @brief Session struct size is reasonable (no accidental bloat).
 */
ZTEST(lora_session_suite, test_session_struct_size)
{
	zassert_true(sizeof(struct lora_session) <= 200, "session size is %zu",
		     sizeof(struct lora_session));
}
