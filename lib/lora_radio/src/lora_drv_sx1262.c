/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(lora_radio);

struct lora_radio_ops {
	int (*init)(void);
	int (*send)(const uint8_t *buf, size_t len);
	int (*recv)(uint8_t *buf, size_t *len, int16_t *rssi, int8_t *snr);
	int (*sleep)(void);
};

static int sx1262_init(void)
{
	return -ENOSYS;
}

static int sx1262_send(const uint8_t *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return -ENOSYS;
}

static int sx1262_recv(uint8_t *buf, size_t *len, int16_t *rssi, int8_t *snr)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	ARG_UNUSED(rssi);
	ARG_UNUSED(snr);
	return -ENOSYS;
}

static int sx1262_sleep(void)
{
	return -ENOSYS;
}

const struct lora_radio_ops lora_sx1262_ops = {
	.init = sx1262_init,
	.send = sx1262_send,
	.recv = sx1262_recv,
	.sleep = sx1262_sleep,
};
