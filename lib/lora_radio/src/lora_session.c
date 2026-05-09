/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lora_radio/lora_radio.h>

LOG_MODULE_DECLARE(lora_radio);

int lora_session_init(void)
{
	return 0;
}

int lora_session_restore(void)
{
	return 0;
}
