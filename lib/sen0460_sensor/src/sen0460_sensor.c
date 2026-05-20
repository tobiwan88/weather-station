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

#include <hw_sensor_utils/hw_sensor_publish.h>
#include <sen0460_sensor/sen0460_sensor.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

#define DT_COMPAT dfr, sen0460

#define HW_SENSOR_BROADCAST_UID 0xFFFFFFFF

struct sen0460_state {
	const struct device *dev;
	uint32_t uid;
	bool enabled;
	bool pm_suspended;
};

static struct sen0460_state sen0460_state;

static void sen0460_trigger_cb(const struct zbus_channel *chan)
{
	const struct sensor_trigger_event *trig = zbus_chan_const_msg(chan);

	if (!sen0460_state.enabled || sen0460_state.pm_suspended) {
		return;
	}
	if (trig->target_uid != 0 && trig->target_uid != sen0460_state.uid &&
	    trig->target_uid != HW_SENSOR_BROADCAST_UID) {
		return;
	}

	LOG_WRN("SEN0460 driver not yet implemented");
}

ZBUS_LISTENER_DEFINE(sen0460_listener, sen0460_trigger_cb);

int sen0460_sensor_enable(void)
{
	if (sen0460_state.dev == NULL) {
		return -ENODEV;
	}

	sen0460_state.enabled = true;
	sen0460_state.pm_suspended = false;
	LOG_INF("SEN0460 enabled (uid=0x%08x)", sen0460_state.uid);
	return 0;
}

int sen0460_sensor_disable(void)
{
	sen0460_state.enabled = false;
	LOG_INF("SEN0460 disabled");
	return 0;
}

#define SEN0460_REGISTRY_ENTRY_DECL(node_id)                                                       \
	static const struct sensor_registry_entry sen0460_reg_##node_id = {                        \
		.uid = DT_PROP_OR(node_id, sensor_uid, CONFIG_SEN0460_SENSOR_DEFAULT_UID),         \
		.label = DT_NODE_FULL_NAME(node_id),                                               \
		.is_remote = false,                                                                \
	};

DT_FOREACH_STATUS_OKAY(DT_COMPAT, SEN0460_REGISTRY_ENTRY_DECL)

#define SEN0460_REGISTRY_REGISTER(node_id)                                                         \
	{                                                                                          \
		int _rc = sensor_registry_register(&sen0460_reg_##node_id);                        \
		if (_rc != 0 && _rc != -EEXIST) {                                                  \
			LOG_ERR("registry register uid 0x%04x failed: %d",                         \
				DT_PROP_OR(node_id, sensor_uid,                                    \
					   CONFIG_SEN0460_SENSOR_DEFAULT_UID),                     \
				_rc);                                                              \
		}                                                                                  \
	}

static int sen0460_sensor_init(void)
{
	sen0460_state.dev = DEVICE_DT_GET_ANY(dfr_sen0460);
	if (!device_is_ready(sen0460_state.dev)) {
		LOG_WRN("SEN0460 device not ready (stub driver)");
		sen0460_state.dev = NULL;
		return 0;
	}

	DT_FOREACH_STATUS_OKAY(DT_COMPAT, SEN0460_REGISTRY_REGISTER)

	sen0460_state.uid =
		DT_PROP_OR(DT_NODELABEL(DT_COMPAT), sensor_uid, CONFIG_SEN0460_SENSOR_DEFAULT_UID);

	int rc = zbus_chan_add_obs(&sensor_trigger_chan, &sen0460_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to add trigger observer: %d", rc);
		return rc;
	}

	sen0460_sensor_enable();

	LOG_INF("SEN0460 stub init done (uid=0x%08x)", sen0460_state.uid);
	return 0;
}

SYS_INIT(sen0460_sensor_init, APPLICATION, 90);
