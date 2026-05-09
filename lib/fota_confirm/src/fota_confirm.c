/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fota_confirm, CONFIG_FOTA_CONFIRM_LOG_LEVEL);

static void confirm_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (boot_is_img_confirmed()) {
		return;
	}

	int rc = boot_write_img_confirmed();

	if (rc == 0) {
		LOG_INF("firmware update confirmed");
	} else {
		LOG_ERR("boot_write_img_confirmed failed: %d", rc);
	}
}

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_work_fn);

static int fota_confirm_init(void)
{
	k_work_schedule(&confirm_work, K_SECONDS(CONFIG_FOTA_CONFIRM_DELAY_S));
	return 0;
}

SYS_INIT(fota_confirm_init, APPLICATION, 99);
