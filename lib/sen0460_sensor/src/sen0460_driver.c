/* SPDX-License-Identifier: Apache-2.0 */

/*
 * sen0460_driver.c — Zephyr sensor driver for DFRobot SEN0460 PM air quality sensor.
 *
 * Layer 1: Upstreamable Zephyr sensor driver implementing the read/decode API
 * (sensor_submit_t + sensor_decoder_api).
 *
 * I2C address: 0x19 (specified in devicetree reg property)
 * Registers 0x05–0x0A form a contiguous 6-byte block (PM1.0, PM2.5, PM10).
 */

#define DT_DRV_COMPAT dfr_sen0460

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/rtio/work.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>

#include <sen0460_sensor/sen0460_sensor.h>

#define LOG_LEVEL CONFIG_SEN0460_SENSOR_LOG_LEVEL
LOG_MODULE_REGISTER(sen0460_driver);

/* SEN0460 register map */
#define SEN0460_REG_POWER       0x01
#define SEN0460_POWER_LOW_POWER 0x01
#define SEN0460_POWER_AWAKE     0x02
#define SEN0460_REG_PM_DATA     0x05
#define SEN0460_PM_DATA_LEN     6
#define SEN0460_REG_FW_VERSION  0x1D

/* Encoded data format: timestamp + 3 × uint16_t raw PM readings */
struct sen0460_encoded_data {
	uint64_t timestamp;
	uint16_t raw_pm1_0;
	uint16_t raw_pm2_5;
	uint16_t raw_pm10;
} __packed;

struct sen0460_data {
	/* No persistent state needed — sensor self-samples continuously */
};

struct sen0460_config {
	struct i2c_dt_spec bus;
};

/* ── RTIO submit (read/decode path) ────────────────────────────────── */

static void sen0460_submit_sync(struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *cfg = iodev_sqe->sqe.iodev->data;
	const struct device *dev = cfg->sensor;
	const struct sen0460_config *config = dev->config;

	uint32_t min_buf_len = sizeof(struct sen0460_encoded_data);
	uint8_t *buf;
	uint32_t buf_len;
	int rc;

	rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
	if (rc != 0) {
		LOG_ERR("Failed to get rx buf (%u bytes)", min_buf_len);
		rtio_iodev_sqe_err(iodev_sqe, rc);
		return;
	}

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("I2C bus not ready");
		rtio_iodev_sqe_err(iodev_sqe, -ENODEV);
		return;
	}

	struct sen0460_encoded_data *edata = (struct sen0460_encoded_data *)buf;
	uint8_t raw[SEN0460_PM_DATA_LEN];

	rc = i2c_burst_read_dt(&config->bus, SEN0460_REG_PM_DATA, raw, sizeof(raw));
	if (rc != 0) {
		LOG_ERR("I2C burst read failed: %d", rc);
		rtio_iodev_sqe_err(iodev_sqe, rc);
		return;
	}

	edata->timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());
	edata->raw_pm1_0 = sys_be16_to_cpu(*(uint16_t *)&raw[0]);
	edata->raw_pm2_5 = sys_be16_to_cpu(*(uint16_t *)&raw[2]);
	edata->raw_pm10 = sys_be16_to_cpu(*(uint16_t *)&raw[4]);

	rtio_iodev_sqe_ok(iodev_sqe, 0);
}

static void sen0460_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	struct rtio_work_req *req = rtio_work_req_alloc();

	if (req == NULL) {
		LOG_ERR("RTIO work req alloc failed, increase CONFIG_RTIO_WORKQ_POOL_ITEMS");
		rtio_iodev_sqe_err(iodev_sqe, -ENOMEM);
		return;
	}

	rtio_work_req_submit(req, iodev_sqe, sen0460_submit_sync);
}

/* ── Decoder API ───────────────────────────────────────────────────── */

static int sen0460_decoder_get_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
					   uint16_t *frame_count)
{
	if (chan_spec.chan_idx != 0) {
		return -ENOTSUP;
	}

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PM_1_0:
	case SENSOR_CHAN_PM_2_5:
	case SENSOR_CHAN_PM_10:
		*frame_count = 1;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int sen0460_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					 size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PM_1_0:
	case SENSOR_CHAN_PM_2_5:
	case SENSOR_CHAN_PM_10:
		*base_size = sizeof(struct sensor_q31_sample_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int sen0460_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				  uint32_t *fit, uint16_t max_count, void *data_out)
{
	if (*fit != 0) {
		return 0;
	}
	if (max_count == 0) {
		return 0;
	}

	struct sensor_q31_data *out = data_out;
	const struct sen0460_encoded_data *edata = (const struct sen0460_encoded_data *)buffer;

	out->header.base_timestamp_ns = edata->timestamp;
	out->header.reading_count = 1;

	uint16_t raw;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_PM_1_0:
		raw = edata->raw_pm1_0;
		out->readings[0].density = (q31_t)(((uint64_t)raw * (uint64_t)INT32_MAX) / 1000);
		break;
	case SENSOR_CHAN_PM_2_5:
		raw = edata->raw_pm2_5;
		out->readings[0].density = (q31_t)(((uint64_t)raw * (uint64_t)INT32_MAX) / 1000);
		break;
	case SENSOR_CHAN_PM_10:
		raw = edata->raw_pm10;
		out->readings[0].density = (q31_t)(((uint64_t)raw * (uint64_t)INT32_MAX) / 1000);
		break;
	default:
		return -EINVAL;
	}

	out->shift = 31;
	*fit = 1;

	return 1;
}

static bool sen0460_decoder_has_trigger(const uint8_t *buffer, enum sensor_trigger_type trigger)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(trigger);
	return false;
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = sen0460_decoder_get_frame_count,
	.get_size_info = sen0460_decoder_get_size_info,
	.decode = sen0460_decoder_decode,
	.has_trigger = sen0460_decoder_has_trigger,
};

int sen0460_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}

/* ── Attribute set (power suspend/resume via private attrs) ────────── */

static int sen0460_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr, const struct sensor_value *val)
{
	const struct sen0460_config *config = dev->config;
	int rc;

	ARG_UNUSED(chan);
	ARG_UNUSED(val);

	switch (attr) {
	case SEN0460_ATTR_SUSPEND:
		rc = i2c_reg_write_byte_dt(&config->bus, SEN0460_REG_POWER,
					   SEN0460_POWER_LOW_POWER);
		if (rc == 0) {
			LOG_INF("SEN0460 suspended (low-power)");
		}
		return rc;
	case SEN0460_ATTR_RESUME:
		rc = i2c_reg_write_byte_dt(&config->bus, SEN0460_REG_POWER, SEN0460_POWER_AWAKE);
		if (rc == 0) {
			LOG_INF("SEN0460 resumed (awake)");
		}
		return rc;
	default:
		return -ENOTSUP;
	}
}

/* ── Legacy API (sample_fetch / channel_get) ───────────────────────── */

static int sen0460_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	/* Read/decode driver — legacy path not implemented.
	 * Use sensor_read() with the decoder API instead. */
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	return -ENOSYS;
}

static int sen0460_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	/* Read/decode driver — legacy path not implemented.
	 * Use sensor_read() with the decoder API instead. */
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(val);
	return -ENOSYS;
}

/* ── Device initialization ─────────────────────────────────────────── */

static int sen0460_init(const struct device *dev)
{
	const struct sen0460_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_WRN("SEN0460 I2C bus not ready");
		return -ENODEV;
	}

	LOG_INF("SEN0460 initialized on I2C");
	return 0;
}

static DEVICE_API(sensor, sen0460_api) = {
	.sample_fetch = sen0460_sample_fetch,
	.channel_get = sen0460_channel_get,
	.attr_set = sen0460_attr_set,
#ifdef CONFIG_SENSOR_ASYNC_API
	.submit = sen0460_submit,
	.get_decoder = sen0460_get_decoder,
#endif
};

#define SEN0460_DEFINE(inst)                                                                       \
	static struct sen0460_data sen0460_data_##inst;                                            \
	static const struct sen0460_config sen0460_config_##inst = {                               \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, sen0460_init, NULL, &sen0460_data_##inst,               \
				     &sen0460_config_##inst, POST_KERNEL,                          \
				     CONFIG_SENSOR_INIT_PRIORITY, &sen0460_api);

DT_INST_FOREACH_STATUS_OKAY(SEN0460_DEFINE)
