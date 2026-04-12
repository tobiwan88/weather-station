/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sntp_sync.c
 * @brief SNTP-based wall-clock synchronisation via SYS_CLOCK_REALTIME.
 *
 * Runs at SYS_INIT APPLICATION priority 80 (before the gateway listener at 95)
 * so that SYS_CLOCK_REALTIME is set before sensor events are first logged.
 *
 * SNTP queries run on a dedicated thread (not the system work queue) so that
 * a slow or unreachable server never stalls other work items (MQTT keepalive,
 * sensor timers, etc.).
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
/* Dedicated SNTP thread                                                       */
/*                                                                             */
/* All SNTP queries run here so the system work queue is never blocked for    */
/* CONFIG_SNTP_SYNC_TIMEOUT_MS while waiting for a network response.          */
/* -------------------------------------------------------------------------- */

static struct k_sem sntp_trigger_sem;

static void sntp_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Initial sync at boot */
	do_sntp_sync();

	while (true) {
#if CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S > 0
		/* Wait for either a manual trigger or the periodic interval;
		 * return value is intentionally ignored — timeout and trigger
		 * both mean "proceed to sync now". */
		(void)k_sem_take(&sntp_trigger_sem, K_SECONDS(CONFIG_SNTP_SYNC_RESYNC_INTERVAL_S));
#else
		/* No periodic resync — wait indefinitely for manual triggers;
		 * return value is intentionally ignored. */
		(void)k_sem_take(&sntp_trigger_sem, K_FOREVER);
#endif
		/* Brief settling delay: on native_sim the HTTP server thread may still
		 * be completing its response (sendto + epoll cleanup) when this thread
		 * wakes.  Sleeping here lets any in-flight socket operations drain before
		 * we open a new UDP socket and add it to the shared NSOS epoll fd. */
		k_sleep(K_MSEC(CONFIG_SNTP_SYNC_PRESYNC_DELAY_MS));
		do_sntp_sync();
	}
}

K_THREAD_DEFINE(sntp_sync_thread, CONFIG_SNTP_SYNC_THREAD_STACK_SIZE, sntp_thread_fn, NULL, NULL,
		NULL, CONFIG_SNTP_SYNC_THREAD_PRIORITY, 0, 0);

/* -------------------------------------------------------------------------- */
/* Immediate resync (API)                                                      */
/* -------------------------------------------------------------------------- */

void sntp_sync_trigger_resync(void)
{
	k_sem_give(&sntp_trigger_sem);
}

/* -------------------------------------------------------------------------- */
/* SYS_INIT                                                                    */
/* -------------------------------------------------------------------------- */

static int sntp_sync_init(void)
{
	k_sem_init(&sntp_trigger_sem, 0, 1);
	return 0;
}

SYS_INIT(sntp_sync_init, APPLICATION, 80);
