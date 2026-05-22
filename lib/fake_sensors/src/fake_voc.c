/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>

#include <fake_sensors/fake_sensors.h>
#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

#ifdef CONFIG_FAKE_SENSORS_JITTER
#	include <zephyr/random/random.h>
#	define FAKE_SENSOR_JITTER(val, max_milli)                                                 \
		((val) + (int32_t)(sys_rand32_get() % (uint32_t)((max_milli) * 2 + 1)) -           \
		 (max_milli))
#else
#	define FAKE_SENSOR_JITTER(val, max_milli) (val)
#endif

/* Jitter range for VOC: ±5 IAQ */
#define FAKE_VOC_JITTER_MILLI 5000

LOG_MODULE_REGISTER(fake_voc, CONFIG_FAKE_SENSORS_LOG_LEVEL);

#define DT_COMPAT     fake_voc
#define DT_DRV_COMPAT DT_COMPAT

#define FAKE_VOC_PUBLISH(entry_ptr)                                                                \
	do {                                                                                       \
		int32_t _mval =                                                                    \
			FAKE_SENSOR_JITTER(*(entry_ptr)->value_milli, FAKE_VOC_JITTER_MILLI);      \
		/* Clamp to valid VOC IAQ range [0, 500 in milli-IAQ] */                           \
		_mval = CLAMP(_mval, 0, 500000);                                                   \
		struct env_sensor_data evt = {                                                     \
			.sensor_uid = (entry_ptr)->uid,                                            \
			.type = SENSOR_TYPE_VOC,                                                   \
			.q31_value = voc_iaq_to_q31(_mval / 1000.0),                               \
			.timestamp_ms = k_uptime_get(),                                            \
		};                                                                                 \
		int rc = zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);                       \
		if (rc != 0) {                                                                     \
			LOG_WRN("uid 0x%08x: pub failed (%d)", (entry_ptr)->uid, rc);              \
		}                                                                                  \
	} while (0)

#define NUM_VOC_INST DT_NUM_INST_STATUS_OKAY(DT_COMPAT)

/* Forward-declare publish function per instance. */
#define FAKE_VOC_PUBLISH_FN_DECL(i, _)                                                             \
	static void fake_voc_publish_##i(struct fake_sensor_entry *entry);

LISTIFY(NUM_VOC_INST, FAKE_VOC_PUBLISH_FN_DECL, ())

/* Per-instance milli-IAQ storage. */
#define FAKE_VOC_DATA_DECL(i, _)                                                                   \
	static int32_t fake_voc_miaq_##i = DT_INST_PROP(i, initial_value_miaq);

LISTIFY(NUM_VOC_INST, FAKE_VOC_DATA_DECL, ())

/* Per-instance fake_sensor_entry in iterable section. */
#define FAKE_VOC_ENTRY_DECL(i, _)                                                                  \
	STRUCT_SECTION_ITERABLE(fake_sensor_entry, fake_voc_entry_##i) = {                         \
		.uid = CONFIG_FAKE_VOC_UID_BASE + i,                                               \
		.kind = FAKE_SENSOR_KIND_VOC,                                                      \
		.label = DT_NODE_FULL_NAME(DT_INST(i, DT_COMPAT)),                                 \
		.value_milli = &fake_voc_miaq_##i,                                                 \
		.publish = fake_voc_publish_##i,                                                   \
	};

LISTIFY(NUM_VOC_INST, FAKE_VOC_ENTRY_DECL, ())

/* Per-instance publish function implementation. */
#define FAKE_VOC_PUBLISH_FN_IMPL(i, _)                                                             \
	static void fake_voc_publish_##i(struct fake_sensor_entry *entry)                          \
	{                                                                                          \
		FAKE_VOC_PUBLISH(entry);                                                           \
		LOG_DBG("uid 0x%08x: %.1f IAQ", entry->uid,                                        \
			(double)(*entry->value_milli) / 1000.0);                                   \
	}

LISTIFY(NUM_VOC_INST, FAKE_VOC_PUBLISH_FN_IMPL, ())

/* --------------------------------------------------------------------------
 * zbus listener callback — shared by all VOC instances.
 * -------------------------------------------------------------------------- */
static void fake_voc_trigger_cb(const struct zbus_channel *chan)
{
	const struct sensor_trigger_event *trig = zbus_chan_const_msg(chan);

	STRUCT_SECTION_FOREACH(fake_sensor_entry, entry)
	{
		if (entry->kind != FAKE_SENSOR_KIND_VOC) {
			continue;
		}
		if (trig->target_uid != 0 && trig->target_uid != entry->uid) {
			continue;
		}
		FAKE_VOC_PUBLISH(entry);
	}
}

ZBUS_LISTENER_DEFINE(fake_voc_listener, fake_voc_trigger_cb);

/* --------------------------------------------------------------------------
 * SYS_INIT: register sensor_trigger_chan observer + sensor_registry entries
 * -------------------------------------------------------------------------- */

#define FAKE_VOC_REGISTRY_ENTRY_DECL(i, _)                                                         \
	static const struct sensor_registry_entry fake_voc_reg_##i = {                             \
		.uid = CONFIG_FAKE_VOC_UID_BASE + i,                                               \
		.label = DT_NODE_FULL_NAME(DT_INST(i, DT_COMPAT)),                                 \
		.is_remote = false,                                                                \
	};

LISTIFY(NUM_VOC_INST, FAKE_VOC_REGISTRY_ENTRY_DECL, ())

#define FAKE_VOC_REGISTRY_REGISTER(i, _)                                                           \
	{                                                                                          \
		int _rc = sensor_registry_register(&fake_voc_reg_##i);                             \
		if (_rc != 0 && _rc != -EEXIST) {                                                  \
			LOG_ERR("registry register uid 0x%08x failed: %d",                         \
				CONFIG_FAKE_VOC_UID_BASE + i, _rc);                                \
		}                                                                                  \
	}

static int fake_voc_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_trigger_chan, &fake_voc_listener, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("Failed to add trigger observer: %d", rc);
		return rc;
	}

	LISTIFY(NUM_VOC_INST, FAKE_VOC_REGISTRY_REGISTER, ())

	LOG_INF("fake_voc: init done");
	return 0;
}

SYS_INIT(fake_voc_init, APPLICATION, 93);
