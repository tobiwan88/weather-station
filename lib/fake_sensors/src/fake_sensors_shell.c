/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/zbus/zbus.h>

#include <fake_sensors/fake_sensors.h>
#include <sensor_registry/sensor_registry.h>
#include <sensor_trigger/sensor_trigger.h>

LOG_MODULE_REGISTER(fake_sensors_shell, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * fake_sensors list
 * -------------------------------------------------------------------------- */
static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%-6s  %-12s  %-20s  %s", "UID", "Kind", "Location", "Current value");
	shell_print(sh, "------  ------------  --------------------  ----------------");

	STRUCT_SECTION_FOREACH(fake_sensor_entry, entry)
	{
		const char *kind_str =
			(entry->kind == FAKE_SENSOR_KIND_TEMPERATURE) ? "temperature" : "humidity";
		const char *unit_str =
			(entry->kind == FAKE_SENSOR_KIND_TEMPERATURE) ? "mdeg C" : "m%RH";
		const char *loc = sensor_registry_get_location(entry->uid);

		shell_print(sh, "0x%04x  %-12s  %-20s  %d %s", entry->uid, kind_str, loc ? loc : "",
			    *entry->value_milli, unit_str);
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * fake_sensors temperature_set <uid_hex> <mdegC>
 * -------------------------------------------------------------------------- */
static int cmd_temperature_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: fake_sensors temperature_set <uid_hex> <mdegC>");
		return -EINVAL;
	}

	uint32_t uid = (uint32_t)strtoul(argv[1], NULL, 16);
	int32_t mdegc = (int32_t)strtol(argv[2], NULL, 10);

	STRUCT_SECTION_FOREACH(fake_sensor_entry, entry)
	{
		if (entry->uid != uid || entry->kind != FAKE_SENSOR_KIND_TEMPERATURE) {
			continue;
		}
		*entry->value_milli = mdegc;
		entry->publish(entry);
		shell_print(sh, "uid 0x%04x: temperature set to %d mdeg C", uid, mdegc);
		return 0;
	}

	shell_error(sh, "No temperature sensor with uid 0x%04x", uid);
	return -ENODEV;
}

/* --------------------------------------------------------------------------
 * fake_sensors humidity_set <uid_hex> <m%RH>
 * -------------------------------------------------------------------------- */
static int cmd_humidity_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: fake_sensors humidity_set <uid_hex> <m_pct_rh>");
		return -EINVAL;
	}

	uint32_t uid = (uint32_t)strtoul(argv[1], NULL, 16);
	int32_t mpct = (int32_t)strtol(argv[2], NULL, 10);

	STRUCT_SECTION_FOREACH(fake_sensor_entry, entry)
	{
		if (entry->uid != uid || entry->kind != FAKE_SENSOR_KIND_HUMIDITY) {
			continue;
		}
		*entry->value_milli = mpct;
		entry->publish(entry);
		shell_print(sh, "uid 0x%04x: humidity set to %d m%%RH", uid, mpct);
		return 0;
	}

	shell_error(sh, "No humidity sensor with uid 0x%04x", uid);
	return -ENODEV;
}

/* --------------------------------------------------------------------------
 * fake_sensors trigger [uid_hex]
 * Broadcasts or targets a manual TRIGGER_SOURCE_BUTTON event.
 * -------------------------------------------------------------------------- */
static int cmd_trigger(const struct shell *sh, size_t argc, char **argv)
{
	struct sensor_trigger_event trig = {
		.source = TRIGGER_SOURCE_BUTTON,
		.target_uid = 0,
	};

	if (argc == 2) {
		trig.target_uid = (uint32_t)strtoul(argv[1], NULL, 16);
	}

	int rc = zbus_chan_pub(&sensor_trigger_chan, &trig, K_MSEC(100));

	if (rc != 0) {
		shell_error(sh, "Failed to publish trigger: %d", rc);
		return rc;
	}

	if (trig.target_uid == 0) {
		shell_print(sh, "Broadcast trigger fired");
	} else {
		shell_print(sh, "Trigger fired for uid 0x%04x", trig.target_uid);
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * Shell command registration
 * -------------------------------------------------------------------------- */
SHELL_STATIC_SUBCMD_SET_CREATE(
	fake_sensors_cmds,
	SHELL_CMD_ARG(list, NULL, "List all registered fake sensors with current values", cmd_list,
		      1, 0),
	SHELL_CMD_ARG(temperature_set, NULL, "Set temperature: <uid_hex> <mdegC>",
		      cmd_temperature_set, 3, 0),
	SHELL_CMD_ARG(humidity_set, NULL, "Set humidity: <uid_hex> <m_pct_rh>", cmd_humidity_set, 3,
		      0),
	SHELL_CMD_ARG(trigger, NULL, "Fire a manual trigger [uid_hex] (omit for broadcast)",
		      cmd_trigger, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fake_sensors, &fake_sensors_cmds, "Fake sensor driver controls", NULL);
