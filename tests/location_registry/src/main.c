/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c (tests/location_registry)
 * @brief Unit tests for location_registry CRUD API.
 *
 * The location_registry supports remove(), so an after_each fixture cleans
 * up all entries between test cases.  The snapshot pattern is required:
 * collect names into a local array inside foreach, then call remove() outside
 * (removing inside foreach would mutate the array being iterated).
 */

#include <location_registry/location_registry.h>
#include <string.h>
#include <zephyr/ztest.h>

/* Maximum locations in the registry (matches CONFIG_LOCATION_REGISTRY_MAX_LOCATIONS default). */
#define MAX_LOCS 8

struct snapshot {
	char names[MAX_LOCS][CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
	int count;
};

static int snapshot_cb(const char *name, void *user_data)
{
	struct snapshot *s = user_data;

	if (s->count < MAX_LOCS) {
		strncpy(s->names[s->count], name, CONFIG_LOCATION_REGISTRY_NAME_LEN);
		s->names[s->count][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';
		s->count++;
	}
	return 0;
}

static void after_each(void *unused)
{
	ARG_UNUSED(unused);

	struct snapshot s = {0};

	location_registry_foreach(snapshot_cb, &s);
	for (int i = 0; i < s.count; i++) {
		location_registry_remove(s.names[i]);
	}
	zassert_equal(location_registry_count(), 0, "after_each: registry not empty");
}

ZTEST_SUITE(location_registry_suite, NULL, NULL, NULL, after_each, NULL);

ZTEST(location_registry_suite, test_add_single)
{
	zassert_equal(location_registry_count(), 0, "registry not empty at start");

	int rc = location_registry_add("kitchen");

	zassert_equal(rc, 0, "add returned %d, expected 0", rc);
	zassert_equal(location_registry_count(), 1, "count should be 1");
	zassert_true(location_registry_exists("kitchen"), "kitchen should exist");
}

ZTEST(location_registry_suite, test_add_multiple)
{
	zassert_equal(location_registry_add("living-room"), 0, "add living-room failed");
	zassert_equal(location_registry_add("bedroom"), 0, "add bedroom failed");
	zassert_equal(location_registry_add("garage"), 0, "add garage failed");

	zassert_equal(location_registry_count(), 3, "count should be 3");
	zassert_true(location_registry_exists("living-room"), "living-room should exist");
	zassert_true(location_registry_exists("bedroom"), "bedroom should exist");
	zassert_true(location_registry_exists("garage"), "garage should exist");
	zassert_false(location_registry_exists("attic"), "attic should not exist");
}

ZTEST(location_registry_suite, test_add_null_returns_einval)
{
	zassert_equal(location_registry_add(NULL), -EINVAL,
		      "add(NULL) should return -EINVAL");
	zassert_equal(location_registry_add(""), -EINVAL,
		      "add(\"\") should return -EINVAL");
	zassert_equal(location_registry_count(), 0, "no entry should have been added");
}

ZTEST(location_registry_suite, test_add_too_long_returns_enametoolong)
{
	/* Name exactly one byte longer than the allowed maximum. */
	char too_long[CONFIG_LOCATION_REGISTRY_NAME_LEN + 2];

	memset(too_long, 'x', CONFIG_LOCATION_REGISTRY_NAME_LEN + 1);
	too_long[CONFIG_LOCATION_REGISTRY_NAME_LEN + 1] = '\0';

	int rc = location_registry_add(too_long);

	zassert_equal(rc, -ENAMETOOLONG, "add(too_long) returned %d, expected -ENAMETOOLONG", rc);
	zassert_equal(location_registry_count(), 0, "no entry should have been added");
}

ZTEST(location_registry_suite, test_add_duplicate_returns_eexist)
{
	zassert_equal(location_registry_add("office"), 0, "first add failed");
	zassert_equal(location_registry_add("office"), -EEXIST,
		      "duplicate add should return -EEXIST");
	zassert_equal(location_registry_count(), 1, "count should remain 1");
}

ZTEST(location_registry_suite, test_remove_existing)
{
	zassert_equal(location_registry_add("porch"), 0, "add porch failed");
	zassert_equal(location_registry_remove("porch"), 0, "remove porch failed");
	zassert_false(location_registry_exists("porch"), "porch should not exist after remove");
	zassert_equal(location_registry_count(), 0, "count should be 0");
}

ZTEST(location_registry_suite, test_remove_missing_returns_enoent)
{
	zassert_equal(location_registry_remove("nonexistent"), -ENOENT,
		      "remove of missing entry should return -ENOENT");
	zassert_equal(location_registry_remove(NULL), -EINVAL,
		      "remove(NULL) should return -EINVAL");
	zassert_equal(location_registry_remove(""), -EINVAL,
		      "remove(\"\") should return -EINVAL");
}

struct order_ctx {
	char names[MAX_LOCS][CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
	int count;
};

static int order_collect_cb(const char *name, void *user_data)
{
	struct order_ctx *ctx = user_data;

	if (ctx->count < MAX_LOCS) {
		strncpy(ctx->names[ctx->count], name, CONFIG_LOCATION_REGISTRY_NAME_LEN);
		ctx->names[ctx->count][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';
		ctx->count++;
	}
	return 0;
}

ZTEST(location_registry_suite, test_remove_middle_preserves_order)
{
	zassert_equal(location_registry_add("alpha"), 0, "add alpha failed");
	zassert_equal(location_registry_add("beta"), 0, "add beta failed");
	zassert_equal(location_registry_add("gamma"), 0, "add gamma failed");

	zassert_equal(location_registry_remove("beta"), 0, "remove beta failed");
	zassert_equal(location_registry_count(), 2, "count should be 2 after remove");

	struct order_ctx ctx = {0};

	location_registry_foreach(order_collect_cb, &ctx);

	zassert_equal(ctx.count, 2, "foreach should yield 2 entries, got %d", ctx.count);
	zassert_equal(strcmp(ctx.names[0], "alpha"), 0, "first entry should be alpha, got %s",
		      ctx.names[0]);
	zassert_equal(strcmp(ctx.names[1], "gamma"), 0, "second entry should be gamma, got %s",
		      ctx.names[1]);
}

struct collect_ctx {
	char names[MAX_LOCS][CONFIG_LOCATION_REGISTRY_NAME_LEN + 1];
	int count;
};

static int collect_cb(const char *name, void *user_data)
{
	struct collect_ctx *ctx = user_data;

	if (ctx->count < MAX_LOCS) {
		strncpy(ctx->names[ctx->count], name, CONFIG_LOCATION_REGISTRY_NAME_LEN);
		ctx->names[ctx->count][CONFIG_LOCATION_REGISTRY_NAME_LEN] = '\0';
		ctx->count++;
	}
	return 0;
}

ZTEST(location_registry_suite, test_foreach_visits_all)
{
	zassert_equal(location_registry_add("hall"), 0, "add hall failed");
	zassert_equal(location_registry_add("pantry"), 0, "add pantry failed");
	zassert_equal(location_registry_add("study"), 0, "add study failed");

	struct collect_ctx ctx = {0};

	location_registry_foreach(collect_cb, &ctx);

	zassert_equal(ctx.count, 3, "foreach should visit 3 entries, got %d", ctx.count);

	bool saw_hall = false, saw_pantry = false, saw_study = false;

	for (int i = 0; i < ctx.count; i++) {
		if (strcmp(ctx.names[i], "hall") == 0) {
			saw_hall = true;
		}
		if (strcmp(ctx.names[i], "pantry") == 0) {
			saw_pantry = true;
		}
		if (strcmp(ctx.names[i], "study") == 0) {
			saw_study = true;
		}
	}
	zassert_true(saw_hall, "foreach missed hall");
	zassert_true(saw_pantry, "foreach missed pantry");
	zassert_true(saw_study, "foreach missed study");
}

static int early_exit_cb(const char *name, void *user_data)
{
	ARG_UNUSED(name);
	int *count = user_data;

	(*count)++;
	return 1; /* stop after first call */
}

ZTEST(location_registry_suite, test_foreach_early_exit)
{
	zassert_equal(location_registry_add("x1"), 0, "add x1 failed");
	zassert_equal(location_registry_add("x2"), 0, "add x2 failed");
	zassert_equal(location_registry_add("x3"), 0, "add x3 failed");

	int count = 0;

	location_registry_foreach(early_exit_cb, &count);

	zassert_equal(count, 1,
		      "foreach with early-exit cb should be called exactly once, got %d", count);
}

ZTEST(location_registry_suite, test_exists_empty_registry)
{
	zassert_equal(location_registry_count(), 0, "registry not empty at start");
	zassert_false(location_registry_exists("anything"),
		      "exists() should return false on empty registry");
}
