/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT fake_lora_radio

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lora_fake, CONFIG_LORA_RADIO_LOG_LEVEL);

#define FAKE_PKT_POOL 16
#define MAX_PKT_SIZE  255

struct fake_packet {
	uint8_t len;
	uint8_t data[MAX_PKT_SIZE];
};

static K_FIFO_DEFINE(fake_tx_fifo);
static K_FIFO_DEFINE(fake_rx_fifo);

static struct fake_packet fake_pool[FAKE_PKT_POOL];
static struct fake_packet *fake_free_list[FAKE_PKT_POOL];
static K_SEM_DEFINE(fake_pool_sem, FAKE_PKT_POOL, FAKE_PKT_POOL);

static struct fake_packet *fake_alloc(void)
{
	if (k_sem_take(&fake_pool_sem, K_NO_WAIT) != 0) {
		return NULL;
	}
	for (int i = 0; i < FAKE_PKT_POOL; i++) {
		if (fake_free_list[i] != NULL) {
			struct fake_packet *pkt = fake_free_list[i];
			fake_free_list[i] = NULL;
			return pkt;
		}
	}
	return NULL;
}

static void fake_free(struct fake_packet *pkt)
{
	int idx = pkt - fake_pool;
	if (idx >= 0 && idx < FAKE_PKT_POOL) {
		fake_free_list[idx] = pkt;
		k_sem_give(&fake_pool_sem);
	}
}

static int fake_config(const struct device *dev, struct lora_modem_config *config)
{
	(void)dev;
	(void)config;
	return 0;
}

static uint32_t fake_airtime(const struct device *dev, uint32_t data_len)
{
	(void)dev;
	(void)data_len;
	return 0;
}

static int fake_send(const struct device *dev, uint8_t *data, uint32_t data_len)
{
	(void)dev;
	struct fake_packet *pkt = fake_alloc();
	if (!pkt) {
		return -ENOMEM;
	}
	pkt->len = MIN(data_len, MAX_PKT_SIZE);
	memcpy(pkt->data, data, pkt->len);
	k_fifo_put(&fake_tx_fifo, pkt);
	LOG_DBG("fake TX: %u bytes", pkt->len);
	return 0;
}

static int fake_recv(const struct device *dev, uint8_t *data, uint8_t size, k_timeout_t timeout,
		     int16_t *rssi, int8_t *snr)
{
	(void)dev;
	struct fake_packet *pkt = k_fifo_get(&fake_rx_fifo, timeout);
	if (!pkt) {
		return -EAGAIN;
	}
	uint8_t copy = MIN(pkt->len, size);
	memcpy(data, pkt->data, copy);
	if (rssi) {
		*rssi = -90;
	}
	if (snr) {
		*snr = 8;
	}
	int ret = (int)copy;
	fake_free(pkt);
	return ret;
}

static int fake_recv_async(const struct device *dev, lora_recv_cb cb, void *user_data)
{
	(void)dev;
	(void)cb;
	(void)user_data;
	return -ENOSYS;
}

static int fake_cad(const struct device *dev, k_timeout_t timeout)
{
	(void)dev;
	(void)timeout;
	return 0;
}

static int fake_test_cw(const struct device *dev, uint32_t frequency, int8_t tx_power,
			uint16_t duration)
{
	(void)dev;
	(void)frequency;
	(void)tx_power;
	(void)duration;
	return 0;
}

static const struct lora_driver_api fake_lora_api = {
	.config = fake_config,
	.airtime = fake_airtime,
	.send = fake_send,
	.recv = fake_recv,
	.recv_async = fake_recv_async,
	.cad = fake_cad,
	.test_cw = fake_test_cw,
};

static int fake_lora_init(const struct device *dev)
{
	(void)dev;
	for (int i = 0; i < FAKE_PKT_POOL; i++) {
		fake_free_list[i] = &fake_pool[i];
	}
	LOG_INF("fake LoRa radio initialized");
	return 0;
}

DEVICE_DT_INST_DEFINE(0, fake_lora_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &fake_lora_api);
