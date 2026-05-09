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

static int fake_init(void)
{
	return 0;
}

static int fake_send(const uint8_t *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return 0;
}

static int fake_recv(uint8_t *buf, size_t *len, int16_t *rssi, int8_t *snr)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	ARG_UNUSED(rssi);
	ARG_UNUSED(snr);
	return -ENOSYS;
}

static int fake_sleep(void)
{
	return 0;
}

const struct lora_radio_ops lora_fake_ops = {
	.init = fake_init,
	.send = fake_send,
	.recv = fake_recv,
	.sleep = fake_sleep,
};
