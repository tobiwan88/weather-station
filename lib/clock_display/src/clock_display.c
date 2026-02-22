/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file clock_display.c
 * @brief Periodic console clock display — logs HH:MM UTC every 60 seconds.
 *
 * Runs at SYS_INIT APPLICATION priority 99 (after SNTP sync at 80) so the
 * first tick already reflects the synchronised wall-clock time.
 */

/* Enable POSIX clock API via host glibc on native_sim (must precede all includes). */
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(clock_display, LOG_LEVEL_INF);

static struct k_work_delayable clock_work;

static void clock_tick(struct k_work *work)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm t = *gmtime(&ts.tv_sec);

	LOG_INF("clock_display: %02d:%02d UTC", t.tm_hour, t.tm_min);

	k_work_reschedule(&clock_work, K_SECONDS(60));
}

static int clock_display_init(void)
{
	k_work_init_delayable(&clock_work, clock_tick);
	/* Fire immediately so the first reading appears at startup. */
	k_work_reschedule(&clock_work, K_NO_WAIT);
	return 0;
}

SYS_INIT(clock_display_init, APPLICATION, 99);
