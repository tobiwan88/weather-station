/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#if defined(CONFIG_FOTA_CONFIRM_REQUIRE_SENSOR_EVENT)
#	include <sensor_event/sensor_event.h>
#endif

LOG_MODULE_REGISTER(fota_confirm, CONFIG_FOTA_CONFIRM_LOG_LEVEL);

static int retry_count;

/* Weak health hook, called before confirmation. Applications may override it
 * with a strong definition of the same signature:
 *
 *     bool fota_confirm_health_hook(void);
 *
 * Returning false defers confirmation. The work is then retried every
 * CONFIG_FOTA_CONFIRM_DELAY_S seconds, up to CONFIG_FOTA_CONFIRM_MAX_RETRIES
 * times. This is not declared in a public header because lib/fota_confirm
 * has no include/ directory.
 */
__weak bool fota_confirm_health_hook(void)
{
	return true;
}

static bool image_healthy(void)
{
	if (!fota_confirm_health_hook()) {
		return false;
	}

#if defined(CONFIG_FOTA_CONFIRM_REQUIRE_SENSOR_EVENT)
	if (zbus_chan_pub_stats_count(&sensor_event_chan) == 0) {
		return false;
	}
#endif

	return true;
}

/* boot_write_img_confirmed() writes only the MCUboot image trailer (a few
 * bytes, no sector erase). It takes the flash controller mutex briefly, which
 * serialises with a concurrent HTTP FOTA upload chunk; keep
 * CONFIG_FOTA_CONFIRM_DELAY_S long enough (30 s in production) that an upload
 * right after boot is unlikely.
 */
static void confirm_work_fn(struct k_work *work)
{
	if (boot_is_img_confirmed()) {
		return;
	}

	int rc = image_healthy() ? boot_write_img_confirmed() : -EAGAIN;

	if (rc == 0) {
		LOG_INF("firmware update confirmed");
		return;
	}

	if (retry_count < CONFIG_FOTA_CONFIRM_MAX_RETRIES) {
		retry_count++;
		LOG_WRN("image not confirmed (%d), retry %d/%d in %d s", rc, retry_count,
			CONFIG_FOTA_CONFIRM_MAX_RETRIES, CONFIG_FOTA_CONFIRM_DELAY_S);
		k_work_reschedule(k_work_delayable_from_work(work),
				  K_SECONDS(CONFIG_FOTA_CONFIRM_DELAY_S));
	} else {
		LOG_ERR("image not confirmed after %d retries (%d); it will revert on next reset",
			CONFIG_FOTA_CONFIRM_MAX_RETRIES, rc);
	}
}

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_work_fn);

static int fota_confirm_init(void)
{
	k_work_schedule(&confirm_work, K_SECONDS(CONFIG_FOTA_CONFIRM_DELAY_S));
	return 0;
}

/* Priority 99 is shared with clock_display and fake_sensors_timer, but there
 * is no ordering dependency: fota_confirm_init only schedules a delayed work
 * item.  Do NOT introduce a dependency on another priority-99 module here.
 */
SYS_INIT(fota_confirm_init, APPLICATION, 99);
