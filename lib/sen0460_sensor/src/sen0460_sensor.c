/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME sen0460_sensor
#define LOG_LEVEL       CONFIG_SEN0460_SENSOR_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>

#include <hw_sensor_utils/hw_sensor_publish.h>
#include <sen0460_sensor/sen0460_sensor.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

#define DT_COMPAT dfr_sen0460

enum sen0460_power_state {
	SEN0460_STATE_SUSPENDED,
	SEN0460_STATE_AWAKENING,
	SEN0460_STATE_SAMPLING,
};

struct sen0460_state {
	const struct device *dev;
	const struct sensor_decoder_api *decoder;
	uint32_t uid;
	bool enabled;
	enum sen0460_power_state power_state;
	struct k_work_delayable settle_work;
};

static struct sen0460_state sen0460_state;

#ifdef CONFIG_SENSOR_ASYNC_API
static struct sensor_chan_spec sen0460_channels[] = {
	{SENSOR_CHAN_PM_1_0, 0},
	{SENSOR_CHAN_PM_2_5, 0},
	{SENSOR_CHAN_PM_10, 0},
};

static struct sensor_read_config sen0460_read_cfg = {
	.is_streaming = false,
	.channels = sen0460_channels,
	.count = ARRAY_SIZE(sen0460_channels),
	.max = ARRAY_SIZE(sen0460_channels),
};

RTIO_IODEV_DEFINE(sen0460_iodev, &__sensor_iodev_api, &sen0460_read_cfg);

RTIO_DEFINE_WITH_MEMPOOL(sen0460_rtio, 4, 4, 4, 256, 4);
#endif

static enum sensor_type zephyr_chan_to_sensor_type(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_PM_1_0:
		return SENSOR_TYPE_PM1_0;
	case SENSOR_CHAN_PM_2_5:
		return SENSOR_TYPE_PM2_5;
	case SENSOR_CHAN_PM_10:
		return SENSOR_TYPE_PM10;
	default:
		return (enum sensor_type) - 1;
	}
}

static void sen0460_sample_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

#ifdef CONFIG_SENSOR_ASYNC_API
	sen0460_state.power_state = SEN0460_STATE_SAMPLING;
	sen0460_read_cfg.sensor = sen0460_state.dev;

	int rc = sensor_read(&sen0460_iodev, &sen0460_rtio, NULL, 0);
	if (rc != 0) {
		LOG_ERR("sensor_read failed: %d", rc);
		goto suspend;
	}

	static const enum sensor_channel channels[] = {
		SENSOR_CHAN_PM_1_0,
		SENSOR_CHAN_PM_2_5,
		SENSOR_CHAN_PM_10,
	};

	struct rtio_cqe *cqe = rtio_cqe_consume_block(&sen0460_rtio);
	if (cqe == NULL || cqe->result != 0) {
		LOG_ERR("sensor read error: %d", cqe ? cqe->result : -ENODATA);
		if (cqe) {
			rtio_cqe_release(&sen0460_rtio, cqe);
		}
		goto suspend;
	}

	uint8_t *buf;
	uint32_t buf_len;

	rc = rtio_cqe_get_mempool_buffer(&sen0460_rtio, cqe, &buf, &buf_len);
	if (rc != 0) {
		LOG_ERR("failed to get mempool buffer: %d", rc);
		rtio_cqe_release(&sen0460_rtio, cqe);
		goto suspend;
	}

	for (size_t i = 0; i < ARRAY_SIZE(channels); i++) {
		struct sensor_chan_spec chan_spec = {channels[i], 0};
		struct sensor_q31_data q31_data;
		uint32_t fit = 0;

		int decoded = sen0460_state.decoder->decode(buf, chan_spec, &fit, 1, &q31_data);
		if (decoded <= 0) {
			LOG_WRN("decode failed for chan %d", channels[i]);
			continue;
		}

		enum sensor_type stype = zephyr_chan_to_sensor_type(channels[i]);
		if (stype == (enum sensor_type) - 1) {
			continue;
		}

		q31_t q31_val = q31_data.readings[0].density;

		hw_sensor_publish(sen0460_state.uid, stype, (int32_t)q31_val);
	}

	rtio_cqe_release(&sen0460_rtio, cqe);

suspend:
#	ifdef CONFIG_PM_DEVICE
	int ret = sensor_attr_set(sen0460_state.dev, SENSOR_CHAN_ALL, SEN0460_ATTR_SUSPEND, NULL);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_WRN("attr_set SUSPEND failed: %d", ret);
	}
#	endif
#else
	LOG_WRN("CONFIG_SENSOR_ASYNC_API not enabled, skipping sample");
#endif

	sen0460_state.power_state = SEN0460_STATE_SUSPENDED;
}

static void sen0460_trigger_cb(const struct zbus_channel *chan)
{
	const struct sensor_trigger_event *trig = zbus_chan_const_msg(chan);

	if (!sen0460_state.enabled) {
		return;
	}
	if (trig->target_uid != 0 && trig->target_uid != sen0460_state.uid &&
	    trig->target_uid != HW_SENSOR_BROADCAST_UID) {
		return;
	}

	if (sen0460_state.power_state != SEN0460_STATE_SUSPENDED) {
		return;
	}

	sen0460_state.power_state = SEN0460_STATE_AWAKENING;

#ifdef CONFIG_PM_DEVICE
	int rc = sensor_attr_set(sen0460_state.dev, SENSOR_CHAN_ALL, SEN0460_ATTR_RESUME, NULL);
	if (rc < 0 && rc != -ENOSYS) {
		LOG_WRN("attr_set RESUME failed: %d", rc);
		sen0460_state.power_state = SEN0460_STATE_SUSPENDED;
		return;
	}
#endif

	k_work_reschedule(&sen0460_state.settle_work, K_MSEC(CONFIG_SEN0460_SENSOR_SETTLE_MS));
}

ZBUS_LISTENER_DEFINE(sen0460_listener, sen0460_trigger_cb);

int sen0460_sensor_enable(void)
{
	if (sen0460_state.dev == NULL) {
		return -ENODEV;
	}

#ifndef CONFIG_SENSOR_ASYNC_API
	LOG_WRN("CONFIG_SENSOR_ASYNC_API not enabled, sensor will not sample");
#endif

#ifdef CONFIG_PM_DEVICE
	int rc = sensor_attr_set(sen0460_state.dev, SENSOR_CHAN_ALL, SEN0460_ATTR_RESUME, NULL);
	if (rc < 0 && rc != -ENOSYS) {
		LOG_WRN("attr_set RESUME failed: %d", rc);
	}
#endif

	sen0460_state.enabled = true;
	sen0460_state.power_state = SEN0460_STATE_SUSPENDED;
	LOG_INF("SEN0460 enabled (uid=0x%08x)", sen0460_state.uid);
	return 0;
}

int sen0460_sensor_disable(void)
{
	k_work_cancel_delayable(&sen0460_state.settle_work);

#ifdef CONFIG_PM_DEVICE
	if (sen0460_state.dev != NULL) {
		int rc = sensor_attr_set(sen0460_state.dev, SENSOR_CHAN_ALL, SEN0460_ATTR_SUSPEND,
					 NULL);
		if (rc < 0 && rc != -ENOSYS) {
			LOG_WRN("attr_set SUSPEND failed: %d", rc);
		}
	}
#endif

	sen0460_state.enabled = false;
	sen0460_state.power_state = SEN0460_STATE_SUSPENDED;
	LOG_INF("SEN0460 disabled");
	return 0;
}

static int sen0460_sensor_init(void)
{
	int rc;

	sen0460_state.dev = DEVICE_DT_GET_ANY(DT_COMPAT);
	if (!device_is_ready(sen0460_state.dev)) {
		LOG_WRN("SEN0460 device not ready");
		sen0460_state.dev = NULL;
		return 0;
	}

#ifdef CONFIG_SENSOR_ASYNC_API
	rc = sen0460_get_decoder(sen0460_state.dev, &sen0460_state.decoder);
	if (rc != 0) {
		LOG_ERR("Failed to get decoder: %d", rc);
		return 0;
	}
#else
	sen0460_state.decoder = NULL;
#endif

	sen0460_state.uid = CONFIG_SEN0460_SENSOR_DEFAULT_UID;
	sen0460_state.power_state = SEN0460_STATE_SUSPENDED;

	{
		static const struct sensor_registry_entry sen0460_reg = {
			.uid = CONFIG_SEN0460_SENSOR_DEFAULT_UID,
			.label = "sen0460",
			.is_remote = false,
		};
		int _rc = sensor_registry_register(&sen0460_reg);
		if (_rc != 0 && _rc != -EEXIST) {
			LOG_ERR("registry register uid 0x%04x failed: %d",
				CONFIG_SEN0460_SENSOR_DEFAULT_UID, _rc);
		}
	}

	k_work_init_delayable(&sen0460_state.settle_work, sen0460_sample_work_fn);

	rc = zbus_chan_add_obs(&sensor_trigger_chan, &sen0460_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to add trigger observer: %d", rc);
		return rc;
	}

	sen0460_sensor_enable();

	LOG_INF("SEN0460 init done (uid=0x%08x)", sen0460_state.uid);
	return 0;
}

SYS_INIT(sen0460_sensor_init, APPLICATION, 90);
