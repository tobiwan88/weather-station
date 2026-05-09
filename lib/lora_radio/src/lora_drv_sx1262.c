/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_sx1262
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include <lora_radio/lora_radio_ops.h>

#define SX1262_CMD_NOP              0x00
#define SX1262_CMD_SET_SLEEP        0x84
#define SX1262_CMD_SET_STANDBY      0x80
#define SX1262_CMD_SET_FS           0xC1
#define SX1262_CMD_SET_TX           0x83
#define SX1262_CMD_SET_RX           0x82
#define SX1262_CMD_SET_RXDUTYCYCLE  0x94
#define SX1262_CMD_SET_CAD          0xC5
#define SX1262_CMD_SET_TXCONTINUOUS 0x93
#define SX1262_CMD_SET_TXCW         0x91
#define SX1262_CMD_SET_MODULATION   0x8B
#define SX1262_CMD_SET_PACKETTYPE   0x8A
#define SX1262_CMD_SET_PACKETPARAMS 0x8C
#define SX1262_CMD_SET_FREQ         0x86
#define SX1262_CMD_SET_TXPARAMS     0x8E
#define SX1262_CMD_SET_CADPARAMS    0x88
#define SX1262_CMD_SET_BUFFERBASE   0x8F
#define SX1262_CMD_GET_STATUS       0xC0
#define SX1262_CMD_GET_RSSI         0xC2
#define SX1262_CMD_GET_SNR          0xC3
#define SX1262_CMD_GET_PACKETSTATUS 0x90
#define SX1262_CMD_READBUFFER       0x1E
#define SX1262_CMD_WRITEBUFFER      0x0E
#define SX1262_CMD_WRITEREG         0x0D
#define SX1262_CMD_READREG          0x1D

#define SPI_DEV  DEVICE_DT_GET(DT_NODELABEL(lora_spi))
#define PIN_NSS  DT_GPIO_PIN(DT_NODELABEL(lora_cs), gpios)
#define PORT_NSS DT_GPIO_LABEL(DT_NODELABEL(lora_cs), gpios)
#define PIN_RST  DT_GPIO_PIN(DT_NODELABEL(lora_rst), gpios)
#define PORT_RST DT_GPIO_LABEL(DT_NODELABEL(lora_rst), gpios)

static const struct device *spi_dev;
static const struct device *gpio_nss;
static const struct device *gpio_rst;
static struct spi_config spi_cfg;

static void sx1262_spi_xfer(const uint8_t *tx, uint8_t *rx, uint8_t len)
{
	struct spi_buf tx_buf = {.buf = (void *)tx, .len = len};
	struct spi_buf rx_buf = {.buf = rx, .len = len};
	struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};
	gpio_pin_set(gpio_nss, PIN_NSS, 0);
	spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
	gpio_pin_set(gpio_nss, PIN_NSS, 1);
}

static void sx1262_cmd(uint8_t cmd, const uint8_t *args, uint8_t arg_len)
{
	uint8_t buf[1 + arg_len];
	buf[0] = cmd;
	memcpy(buf + 1, args, arg_len);
	sx1262_spi_xfer(buf, NULL, sizeof(buf));
}

static int sx1262_init(void)
{
	spi_dev = DEVICE_DT_GET(DT_NODELABEL(lora_spi));
	gpio_nss = device_get_binding(PORT_NSS);
	gpio_rst = device_get_binding(PORT_RST);
	if (!device_is_ready(spi_dev) || !gpio_nss || !gpio_rst) {
		LOG_ERR("SPI or GPIO device not ready");
		return -ENODEV;
	}

	gpio_pin_configure(gpio_nss, PIN_NSS, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure(gpio_rst, PIN_RST, GPIO_OUTPUT_INACTIVE);

	spi_cfg.frequency = 8000000;
	spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER;
	spi_cfg.slave = 0;
	spi_cfg.cs = NULL;

	gpio_pin_set(gpio_rst, PIN_RST, 0);
	k_sleep(K_USEC(100));
	gpio_pin_set(gpio_rst, PIN_RST, 1);
	k_sleep(K_MSEC(5));

	uint8_t pkt_type = 1;
	sx1262_cmd(SX1262_CMD_SET_PACKETTYPE, &pkt_type, 1);

	uint8_t pp[] = {8, 0, 0, 2, 1, 0};
	sx1262_cmd(SX1262_CMD_SET_PACKETPARAMS, pp, sizeof(pp));

	LOG_INF("SX1262 initialized");
	return 0;
}

static int sx1262_set_frequency(uint32_t freq_hz)
{
	uint32_t frf = (uint32_t)((uint64_t)freq_hz << 25) / 32000000;
	uint8_t args[] = {(frf >> 16) & 0xFF, (frf >> 8) & 0xFF, frf & 0xFF};
	sx1262_cmd(SX1262_CMD_SET_FREQ, args, 3);
	return 0;
}

static int sx1262_set_modem_config(uint8_t sf, uint32_t bw)
{
	uint8_t bw_idx;
	if (bw <= 10)
		bw_idx = 0;
	else if (bw <= 20)
		bw_idx = 1;
	else if (bw <= 62)
		bw_idx = 3;
	else if (bw <= 125)
		bw_idx = 7;
	else if (bw <= 250)
		bw_idx = 8;
	else
		bw_idx = 9;

	uint8_t args[] = {sf, bw_idx, 0x00, 0x01, 0x00};
	sx1262_cmd(SX1262_CMD_SET_MODULATION, args, sizeof(args));
	return 0;
}

static int sx1262_set_tx_power(int8_t dbm)
{
	uint8_t args[] = {(uint8_t)(MIN(MAX(dbm + 18, 0), 31)), 0x04};
	sx1262_cmd(SX1262_CMD_SET_TXPARAMS, args, 2);
	return 0;
}

static int sx1262_tx(const uint8_t *data, uint8_t len)
{
	uint8_t wbuf[2] = {SX1262_CMD_WRITEBUFFER, 0};
	sx1262_spi_xfer(wbuf, NULL, 2);
	sx1262_spi_xfer(data, NULL, len);

	uint8_t tx_args[3] = {len, 0, 0};
	sx1262_cmd(SX1262_CMD_SET_TX, tx_args, 3);
	return 0;
}

static int sx1262_rx(uint8_t *buf, uint8_t max_len, int32_t timeout_ms)
{
	uint8_t rx_args[3] = {0, 0, 0};
	sx1262_cmd(SX1262_CMD_SET_RX, rx_args, 3);

	if (timeout_ms > 0 && timeout_ms != SYS_FOREVER_MS) {
		k_sleep(K_MSEC(timeout_ms));
	}

	uint8_t rx_status[2];
	sx1262_spi_xfer((uint8_t[]){SX1262_CMD_GET_PACKETSTATUS, 0}, rx_status, 2);
	uint8_t pkt_len = rx_status[0];
	if (pkt_len > max_len)
		pkt_len = max_len;

	uint8_t rbuf[2] = {SX1262_CMD_READBUFFER, 0};
	sx1262_spi_xfer(rbuf, NULL, 2);
	sx1262_spi_xfer(NULL, buf, pkt_len);
	return pkt_len;
}

static int sx1262_rx_enable(bool enable)
{
	if (enable) {
		uint8_t rx_args[3] = {0, 0, 0};
		sx1262_cmd(SX1262_CMD_SET_RX, rx_args, 3);
	} else {
		sx1262_cmd(SX1262_CMD_SET_STANDBY, (uint8_t[]){0}, 1);
	}
	return 0;
}

static int16_t sx1262_rssi(void)
{
	uint8_t reg[2];
	sx1262_spi_xfer((uint8_t[]){SX1262_CMD_GET_RSSI, 0}, reg, 2);
	return (int16_t)(-(int)(reg[0] / 2));
}

static int8_t sx1262_snr(void)
{
	uint8_t reg[2];
	sx1262_spi_xfer((uint8_t[]){SX1262_CMD_GET_SNR, 0}, reg, 2);
	return (int8_t)reg[0];
}

const struct lora_radio_ops lora_sx1262_radio_ops = {
	.init = sx1262_init,
	.set_frequency = sx1262_set_frequency,
	.set_modem_config = sx1262_set_modem_config,
	.set_tx_power = sx1262_set_tx_power,
	.tx = sx1262_tx,
	.rx = sx1262_rx,
	.rx_enable = sx1262_rx_enable,
	.rssi = sx1262_rssi,
	.snr = sx1262_snr,
};
