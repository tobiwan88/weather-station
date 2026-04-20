/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file clock_display.c
 * @brief Periodic console clock display — logs HH:MM UTC every 60 seconds.
 *
 * Runs at SYS_INIT APPLICATION priority 99 (after SNTP sync at 80) so the
 * first tick already reflects the synchronised wall-clock time.
 */

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

LOG_MODULE_REGISTER(clock_display, CONFIG_CLOCK_DISPLAY_LOG_LEVEL);

static struct k_work_delayable clock_work;

static void clock_tick(struct k_work *work)
{
	struct timespec ts;

	sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	int64_t sod = ts.tv_sec % 86400;
	int hour = (int)(sod / 3600);
	int min = (int)((sod % 3600) / 60);

	LOG_INF("clock_display: %02d:%02d UTC", hour, min);

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
