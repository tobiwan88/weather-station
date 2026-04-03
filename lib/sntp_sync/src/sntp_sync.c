/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sntp_sync.c
 * @brief SNTP-based wall-clock synchronisation via SYS_CLOCK_REALTIME.
 *
 * Runs at SYS_INIT APPLICATION priority 80 (before the gateway listener at 95)
 * so that SYS_CLOCK_REALTIME is set before sensor events are first logged.
 *
 * On failure, logs a WRN and continues — the clock stays at boot epoch but
 * the rest of the application is unaffected.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>

#include <sntp_sync/sntp_sync.h>

LOG_MODULE_REGISTER(sntp_sync, LOG_LEVEL_INF);

static atomic_t synced;

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

bool sntp_sync_is_ready(void)
{
	return atomic_get(&synced) != 0;
}

int64_t sntp_sync_get_epoch_ms(void)
{
	struct timespec ts;

	sys_clock_gettime(SYS_CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* -------------------------------------------------------------------------- */
/* Internal sync logic                                                         */
/* -------------------------------------------------------------------------- */

static int do_sntp_sync(void)
{
	struct sntp_time sntp_time;
	int rc;

	rc = sntp_simple(CONFIG_SNTP_SYNC_SERVER, CONFIG_SNTP_SYNC_TIMEOUT_MS, &sntp_time);
	if (rc != 0) {
		LOG_WRN("SNTP sync failed (server=%s rc=%d); clock stays at boot time",
			CONFIG_SNTP_SYNC_SERVER, rc);
		return rc;
	}

	struct timespec ts = {
		.tv_sec = (time_t)sntp_time.seconds,
		.tv_nsec = ((long)sntp_time.fraction * 1000000000LL) >> 32,
	};

	rc = sys_clock_settime(SYS_CLOCK_REALTIME, &ts);
	if (rc != 0) {
		LOG_WRN("sys_clock_settime failed: rc=%d", rc);
		return rc;
	}

	atomic_set(&synced, 1);
	LOG_INF("sntp_sync: synced to %s, epoch=%lld", CONFIG_SNTP_SYNC_SERVER,
		(long long)sntp_time.seconds);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Periodic resync via delayable work                                          */
/* -------------------------------------------------------------------------- */

#if CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S > 0

static struct k_work_delayable resync_work;

static void resync_handler(struct k_work *work)
{
	do_sntp_sync();
	k_work_reschedule(&resync_work, K_SECONDS(CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S));
}

#endif /* CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S > 0 */

/* -------------------------------------------------------------------------- */
/* Immediate resync (API)                                                      */
/* -------------------------------------------------------------------------- */

static struct k_work_delayable immediate_work;

static void immediate_resync_handler(struct k_work *work)
{
	do_sntp_sync();
}

void sntp_sync_trigger_resync(void)
{
	k_work_reschedule(&immediate_work, K_NO_WAIT);
}

/* -------------------------------------------------------------------------- */
/* SYS_INIT                                                                    */
/* -------------------------------------------------------------------------- */

static int sntp_sync_init(void)
{
	do_sntp_sync();

#if CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S > 0
	k_work_init_delayable(&resync_work, resync_handler);
	k_work_reschedule(&resync_work, K_SECONDS(CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S));
#endif

	k_work_init_delayable(&immediate_work, immediate_resync_handler);

	return 0;
}

SYS_INIT(sntp_sync_init, APPLICATION, 80);
