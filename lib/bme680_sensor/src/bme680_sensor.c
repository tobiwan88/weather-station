/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME bme680_sensor
#define LOG_LEVEL       CONFIG_BME680_SENSOR_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <bme680_sensor/bme680_sensor.h>
#include <hw_sensor_utils/hw_sensor_publish.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

#define DT_COMPAT bosch_bme680

struct bme680_state {
	const struct device *dev;
	uint32_t uid;
	bool enabled;
};

static struct bme680_state bme680_state;

static enum sensor_type zephyr_chan_to_sensor_type(enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		return SENSOR_TYPE_TEMPERATURE;
	case SENSOR_CHAN_HUMIDITY:
		return SENSOR_TYPE_HUMIDITY;
	case SENSOR_CHAN_PRESS:
		return SENSOR_TYPE_PRESSURE;
	case SENSOR_CHAN_GAS_RES:
		return SENSOR_TYPE_GAS_RESISTANCE;
	default:
		return (enum sensor_type) - 1;
	}
}

static int32_t encode_q31(enum sensor_type type, const struct sensor_value *val)
{
	double phys = sensor_value_to_double(val);

	switch (type) {
	case SENSOR_TYPE_TEMPERATURE:
		return temperature_c_to_q31(phys);
	case SENSOR_TYPE_HUMIDITY:
		return humidity_pct_to_q31(phys);
	case SENSOR_TYPE_PRESSURE:
		return pressure_hpa_to_q31(phys);
	case SENSOR_TYPE_GAS_RESISTANCE:
		return gas_resistance_ohm_to_q31(phys);
	default:
		return 0;
	}
}

static void bme680_trigger_cb(const struct zbus_channel *chan)
{
	const struct sensor_trigger_event *trig = zbus_chan_const_msg(chan);

	if (!bme680_state.enabled) {
		return;
	}
	if (trig->target_uid != 0 && trig->target_uid != bme680_state.uid &&
	    trig->target_uid != HW_SENSOR_BROADCAST_UID) {
		return;
	}

	int ret = sensor_sample_fetch(bme680_state.dev);
	if (ret < 0) {
		LOG_ERR("sensor_sample_fetch failed: %d", ret);
		return;
	}

	static const enum sensor_channel channels[] = {
		SENSOR_CHAN_AMBIENT_TEMP,
		SENSOR_CHAN_HUMIDITY,
		SENSOR_CHAN_PRESS,
		SENSOR_CHAN_GAS_RES,
	};

	for (size_t i = 0; i < ARRAY_SIZE(channels); i++) {
		struct sensor_value val;

		ret = sensor_channel_get(bme680_state.dev, channels[i], &val);
		if (ret < 0) {
			LOG_WRN("channel_get chan %d failed: %d", channels[i], ret);
			continue;
		}

		enum sensor_type stype = zephyr_chan_to_sensor_type(channels[i]);
		if (stype == (enum sensor_type) - 1) {
			continue;
		}

		int32_t q31 = encode_q31(stype, &val);
		hw_sensor_publish(bme680_state.uid, stype, q31);
	}
}

ZBUS_LISTENER_DEFINE(bme680_listener, bme680_trigger_cb);

int bme680_sensor_enable(void)
{
	if (bme680_state.dev == NULL) {
		return -ENODEV;
	}

#ifdef CONFIG_PM_DEVICE
	int ret = pm_device_action_run(bme680_state.dev, PM_DEVICE_ACTION_RESUME);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_WRN("pm_device resume failed: %d", ret);
	}
#endif

	bme680_state.enabled = true;
	LOG_INF("BME680 enabled (uid=0x%08x)", bme680_state.uid);
	return 0;
}

int bme680_sensor_disable(void)
{
	bme680_state.enabled = false;

#ifdef CONFIG_PM_DEVICE
	if (bme680_state.dev != NULL) {
		int ret = pm_device_action_run(bme680_state.dev, PM_DEVICE_ACTION_SUSPEND);
		if (ret < 0 && ret != -ENOSYS) {
			LOG_WRN("pm_device suspend failed: %d", ret);
		}
	}
#endif

	LOG_INF("BME680 disabled");
	return 0;
}

static int bme680_sensor_init(void)
{
	bme680_state.dev = DEVICE_DT_GET_ANY(DT_COMPAT);
	if (!device_is_ready(bme680_state.dev)) {
		LOG_WRN("BME680 device not ready");
		bme680_state.dev = NULL;
		return 0;
	}

	bme680_state.uid = CONFIG_BME680_SENSOR_DEFAULT_UID;

	{
		static const struct sensor_registry_entry bme680_reg = {
			.uid = CONFIG_BME680_SENSOR_DEFAULT_UID,
			.label = DT_NODE_FULL_NAME(DT_DRV_INST(0)),
			.is_remote = false,
		};
		int _rc = sensor_registry_register(&bme680_reg);
		if (_rc != 0 && _rc != -EEXIST) {
			LOG_ERR("registry register uid 0x%04x failed: %d",
				CONFIG_BME680_SENSOR_DEFAULT_UID, _rc);
		}
	}

	int rc = zbus_chan_add_obs(&sensor_trigger_chan, &bme680_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to add trigger observer: %d", rc);
		return rc;
	}

	bme680_sensor_enable();

	LOG_INF("BME680 init done (uid=0x%08x)", bme680_state.uid);
	return 0;
}

SYS_INIT(bme680_sensor_init, APPLICATION, 90);
