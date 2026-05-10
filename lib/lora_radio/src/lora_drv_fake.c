/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fake
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>

#include <lora_radio/lora_radio_ops.h>

static K_FIFO_DEFINE(lora_fake_tx_fifo);
static K_FIFO_DEFINE(lora_fake_rx_fifo);

struct fake_packet {
	struct k_fifo _fifo;
	uint8_t len;
	uint8_t data[];
};

static int fake_init(void)
{
	LOG_INF("fake radio initialized");
	return 0;
}

static int fake_set_frequency(uint32_t freq_hz)
{
	(void)freq_hz;
	return 0;
}

static int fake_set_modem_config(uint8_t sf, uint32_t bw)
{
	(void)sf;
	(void)bw;
	return 0;
}

static int fake_set_tx_power(int8_t dbm)
{
	(void)dbm;
	return 0;
}

static int fake_tx(const uint8_t *data, uint8_t len)
{
	struct fake_packet *pkt = k_malloc(sizeof(*pkt) + len);
	if (!pkt) {
		return -ENOMEM;
	}
	pkt->len = len;
	memcpy(pkt->data, data, len);
	k_fifo_put(&lora_fake_tx_fifo, pkt);
	LOG_DBG("fake TX: %d bytes", len);
	return 0;
}

static int fake_rx(uint8_t *buf, uint8_t max_len, int32_t timeout_ms)
{
	struct fake_packet *pkt;
	if (timeout_ms == SYS_FOREVER_US || timeout_ms == SYS_FOREVER_MS) {
		pkt = k_fifo_get(&lora_fake_rx_fifo, K_FOREVER);
	} else {
		pkt = k_fifo_get(&lora_fake_rx_fifo, K_MSEC(timeout_ms));
	}
	if (!pkt) {
		return -EAGAIN;
	}
	uint8_t copy = MIN(pkt->len, max_len);
	memcpy(buf, pkt->data, copy);
	int ret = copy;
	k_free(pkt);
	return ret;
}

static int fake_rx_enable(bool enable)
{
	(void)enable;
	return 0;
}

static int16_t fake_rssi(void)
{
	return -90;
}

static int8_t fake_snr(void)
{
	return 8;
}

const struct lora_radio_ops lora_fake_radio_ops = {
	.init = fake_init,
	.set_frequency = fake_set_frequency,
	.set_modem_config = fake_set_modem_config,
	.set_tx_power = fake_set_tx_power,
	.tx = fake_tx,
	.rx = fake_rx,
	.rx_enable = fake_rx_enable,
	.rssi = fake_rssi,
	.snr = fake_snr,
};
