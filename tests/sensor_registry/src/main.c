/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/sensor_registry)
 * @brief Unit tests for sensor_registry API.
 *
 * The registry is a process-lifetime static array with no clear/reset API.
 * UIDs are globally unique across all test cases and never reused.
 * Tests run sequentially and tolerate a monotonically increasing count.
 *
 * UID allocation:
 *   0xBEEF0001 — local temperature sensor (register/lookup)
 *   0xBEEF0002 — remote sensor (is_remote=true round-trip)
 *   0xBEEF0003 — used for duplicate-register test
 *   0xBEEF0004 — used for count-increases test
 *   0xBEEF0099 — never registered (lookup-miss test)
 *   0xBEEF00A1/A2 — foreach-local entries
 *   0xBEEF0100–0xBEEF0109 — filler entries for registry-full test
 *   0xBEEF010A — overflow probe (never successfully registered)
 */

#include <sensor_registry/sensor_registry.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(sensor_registry_suite, NULL, NULL, NULL, NULL, NULL);

/* All entries are static so the registry's stored pointer stays valid. */
static const struct sensor_registry_entry entry_temp = {
	.uid = 0xBEEF0001,
	.label = "test-temp",
	.is_remote = false,
};

static const struct sensor_registry_entry entry_remote = {
	.uid = 0xBEEF0002,
	.label = "test-remote",
	.is_remote = true,
};

static const struct sensor_registry_entry entry_dup1 = {
	.uid = 0xBEEF0003,
	.label = "dup-first",
	.is_remote = false,
};

static const struct sensor_registry_entry entry_dup2 = {
	.uid = 0xBEEF0003,
	.label = "dup-second",
	.is_remote = false,
};

static const struct sensor_registry_entry entry_count = {
	.uid = 0xBEEF0004,
	.label = "test-count",
	.is_remote = false,
};

ZTEST(sensor_registry_suite, test_register_and_lookup)
{
	int rc = sensor_registry_register(&entry_temp);

	zassert_equal(rc, 0, "register returned %d, expected 0", rc);

	const struct sensor_registry_entry *result = sensor_registry_lookup(0xBEEF0001);

	zassert_not_null(result, "lookup returned NULL for registered uid");
	zassert_equal(result->uid, 0xBEEF0001, "uid mismatch");
	zassert_equal(strcmp(result->label, "test-temp"), 0, "label mismatch");
	zassert_false(result->is_remote, "is_remote should be false");
}

ZTEST(sensor_registry_suite, test_register_remote_sensor)
{
	int rc = sensor_registry_register(&entry_remote);

	zassert_equal(rc, 0, "register returned %d, expected 0", rc);

	const struct sensor_registry_entry *result = sensor_registry_lookup(0xBEEF0002);

	zassert_not_null(result, "lookup returned NULL for registered remote uid");
	zassert_true(result->is_remote, "is_remote should be true");
}

ZTEST(sensor_registry_suite, test_lookup_missing_uid)
{
	const struct sensor_registry_entry *result = sensor_registry_lookup(0xBEEF0099);

	zassert_is_null(result, "lookup of unregistered uid should return NULL");
}

ZTEST(sensor_registry_suite, test_register_null_entry_returns_einval)
{
	int rc = sensor_registry_register(NULL);

	zassert_equal(rc, -EINVAL, "register(NULL) returned %d, expected -EINVAL", rc);

	static const struct sensor_registry_entry entry_no_label = {
		.uid = 0xBEEF0010,
		.label = NULL,
		.is_remote = false,
	};
	rc = sensor_registry_register(&entry_no_label);
	zassert_equal(rc, -EINVAL, "register(label=NULL) returned %d, expected -EINVAL", rc);
}

ZTEST(sensor_registry_suite, test_register_duplicate_uid_returns_eexist)
{
	int rc = sensor_registry_register(&entry_dup1);

	zassert_equal(rc, 0, "first register returned %d, expected 0", rc);

	rc = sensor_registry_register(&entry_dup2);
	zassert_equal(rc, -EEXIST, "duplicate register returned %d, expected -EEXIST", rc);
}

ZTEST(sensor_registry_suite, test_count_increases_with_registration)
{
	int count_before = sensor_registry_count();

	zassert_true(count_before >= 0, "count should be non-negative");

	int rc = sensor_registry_register(&entry_count);

	zassert_equal(rc, 0, "register returned %d, expected 0", rc);
	zassert_equal(sensor_registry_count(), count_before + 1,
		      "count should have increased by 1");
}

/*
 * Entries unique to test_foreach_visits_all so it is self-contained.
 * ztest runs tests alphabetically, so prior tests may not have registered
 * the other UIDs yet.  Using dedicated UIDs avoids the ordering dependency.
 */
static const struct sensor_registry_entry entry_fa = {
	.uid = 0xBEEF00A1,
	.label = "foreach-a",
	.is_remote = false,
};

static const struct sensor_registry_entry entry_fb = {
	.uid = 0xBEEF00A2,
	.label = "foreach-b",
	.is_remote = true,
};

struct foreach_ctx {
	int call_count;
	bool saw_beefa1;
	bool saw_beefa2;
};

static int foreach_collect_cb(const struct sensor_registry_entry *e, void *user_data)
{
	struct foreach_ctx *ctx = user_data;

	ctx->call_count++;
	if (e->uid == 0xBEEF00A1) {
		ctx->saw_beefa1 = true;
	}
	if (e->uid == 0xBEEF00A2) {
		ctx->saw_beefa2 = true;
	}
	return 0;
}

ZTEST(sensor_registry_suite, test_foreach_visits_all)
{
	/* Register test-local entries; -EEXIST is fine if this test ever re-runs. */
	int rc = sensor_registry_register(&entry_fa);

	zassert_true(rc == 0 || rc == -EEXIST, "register entry_fa returned %d", rc);
	rc = sensor_registry_register(&entry_fb);
	zassert_true(rc == 0 || rc == -EEXIST, "register entry_fb returned %d", rc);

	struct foreach_ctx ctx = {0};

	sensor_registry_foreach(foreach_collect_cb, &ctx);

	zassert_true(ctx.call_count >= 2, "foreach called %d times, expected >= 2",
		     ctx.call_count);
	zassert_true(ctx.saw_beefa1, "foreach missed uid 0xBEEF00A1");
	zassert_true(ctx.saw_beefa2, "foreach missed uid 0xBEEF00A2");
}

static int foreach_stop_cb(const struct sensor_registry_entry *e, void *user_data)
{
	ARG_UNUSED(e);
	int *count = user_data;

	(*count)++;
	return 1; /* stop after first entry */
}

ZTEST(sensor_registry_suite, test_foreach_early_exit)
{
	int count = 0;

	sensor_registry_foreach(foreach_stop_cb, &count);

	zassert_equal(count, 1, "foreach with early-exit cb should be called exactly once, got %d",
		      count);
}

/*
 * test_z_register_full_registry_returns_enomem
 *
 * The 'z_' prefix ensures this test runs last alphabetically, after all other
 * tests have already registered their UIDs (6 entries: 0xBEEF0001–0xBEEF0004,
 * 0xBEEF00A1, 0xBEEF00A2).  We fill the remaining 10 slots with filler
 * entries to reach SENSOR_REGISTRY_MAX_ENTRIES (16), then verify that the
 * next register() call returns -ENOMEM.
 */
static const struct sensor_registry_entry fill_entries[10] = {
	{.uid = 0xBEEF0100, .label = "fill-0", .is_remote = false},
	{.uid = 0xBEEF0101, .label = "fill-1", .is_remote = false},
	{.uid = 0xBEEF0102, .label = "fill-2", .is_remote = false},
	{.uid = 0xBEEF0103, .label = "fill-3", .is_remote = false},
	{.uid = 0xBEEF0104, .label = "fill-4", .is_remote = false},
	{.uid = 0xBEEF0105, .label = "fill-5", .is_remote = false},
	{.uid = 0xBEEF0106, .label = "fill-6", .is_remote = false},
	{.uid = 0xBEEF0107, .label = "fill-7", .is_remote = false},
	{.uid = 0xBEEF0108, .label = "fill-8", .is_remote = false},
	{.uid = 0xBEEF0109, .label = "fill-9", .is_remote = false},
};

static const struct sensor_registry_entry overflow_entry = {
	.uid = 0xBEEF010A,
	.label = "overflow",
	.is_remote = false,
};

ZTEST(sensor_registry_suite, test_z_register_full_registry_returns_enomem)
{
	/* Register filler entries until the registry is full. */
	for (int i = 0; i < 10; i++) {
		int rc = sensor_registry_register(&fill_entries[i]);

		zassert_equal(rc, 0, "filler entry %d returned %d, expected 0", i, rc);
	}

	zassert_equal(sensor_registry_count(), SENSOR_REGISTRY_MAX_ENTRIES,
		      "registry should be full (%d entries)", SENSOR_REGISTRY_MAX_ENTRIES);

	int rc = sensor_registry_register(&overflow_entry);

	zassert_equal(rc, -ENOMEM, "register on full registry returned %d, expected -ENOMEM", rc);
}
