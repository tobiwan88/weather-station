/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file remote_sensor_shell.c
 * @brief Shell commands for remote sensor management.
 *
 * Commands:
 *   remote_sensor list               — list registered remote sensors
 *   remote_sensor scan start [proto] — start scanning (empty = all)
 *   remote_sensor scan stop  [proto] — stop scanning
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include <remote_sensor/remote_sensor.h>
#include <sensor_registry/sensor_registry.h>

LOG_MODULE_DECLARE(remote_sensor, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * Helper: parse protocol name → enum
 * -------------------------------------------------------------------------- */

static enum remote_transport_proto proto_from_str(const char *s)
{
	if (!s || s[0] == '\0') {
		return REMOTE_TRANSPORT_PROTO_UNKNOWN;
	}
	if (strcmp(s, "ble") == 0) {
		return REMOTE_TRANSPORT_PROTO_BLE;
	}
	if (strcmp(s, "lora") == 0) {
		return REMOTE_TRANSPORT_PROTO_LORA;
	}
	if (strcmp(s, "thread") == 0) {
		return REMOTE_TRANSPORT_PROTO_THREAD;
	}
	if (strcmp(s, "fake") == 0) {
		return REMOTE_TRANSPORT_PROTO_FAKE;
	}
	return REMOTE_TRANSPORT_PROTO_UNKNOWN;
}

static const char *proto_to_str(enum remote_transport_proto proto)
{
	switch (proto) {
	case REMOTE_TRANSPORT_PROTO_BLE:
		return "ble";
	case REMOTE_TRANSPORT_PROTO_LORA:
		return "lora";
	case REMOTE_TRANSPORT_PROTO_THREAD:
		return "thread";
	case REMOTE_TRANSPORT_PROTO_FAKE:
		return "fake";
	default:
		return "unknown";
	}
}

/* --------------------------------------------------------------------------
 * remote_sensor list
 * -------------------------------------------------------------------------- */

struct list_cb_ctx {
	const struct shell *sh;
	int count;
};

static int list_cb(const struct sensor_registry_entry *entry, void *user_data)
{
	struct list_cb_ctx *ctx = user_data;

	if (!entry->is_remote) {
		return 0;
	}

	const char *name = sensor_registry_get_display_name(entry->uid);

	shell_print(ctx->sh, "  uid=0x%08x  %-30s  %s", entry->uid, name ? name : "(unknown)",
		    entry->label);
	ctx->count++;
	return 0;
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct list_cb_ctx ctx = {.sh = sh, .count = 0};

	shell_print(sh, "Remote sensors:");
	sensor_registry_foreach(list_cb, &ctx);
	if (ctx.count == 0) {
		shell_print(sh, "  (none registered)");
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * remote_sensor scan start/stop [proto]
 * -------------------------------------------------------------------------- */

static int cmd_scan_start(const struct shell *sh, size_t argc, char **argv)
{
	enum remote_transport_proto proto = REMOTE_TRANSPORT_PROTO_UNKNOWN;

	if (argc > 1) {
		proto = proto_from_str(argv[1]);
		if (proto == REMOTE_TRANSPORT_PROTO_UNKNOWN && argv[1][0] != '\0') {
			shell_error(sh,
				    "Unknown protocol '%s'. "
				    "Valid: ble, lora, thread, fake",
				    argv[1]);
			return -EINVAL;
		}
	}

	struct remote_scan_ctrl_event evt = {
		.action = REMOTE_SCAN_START,
		.proto = proto,
	};
	int rc = zbus_chan_pub(&remote_scan_ctrl_chan, &evt, K_MSEC(100));

	if (rc != 0) {
		shell_error(sh, "Failed to publish scan start: %d", rc);
		return rc;
	}

	if (proto == REMOTE_TRANSPORT_PROTO_UNKNOWN) {
		shell_print(sh, "Scan started on all transports");
	} else {
		shell_print(sh, "Scan started on %s", proto_to_str(proto));
	}
	return 0;
}

static int cmd_scan_stop(const struct shell *sh, size_t argc, char **argv)
{
	enum remote_transport_proto proto = REMOTE_TRANSPORT_PROTO_UNKNOWN;

	if (argc > 1) {
		proto = proto_from_str(argv[1]);
		if (proto == REMOTE_TRANSPORT_PROTO_UNKNOWN && argv[1][0] != '\0') {
			shell_error(sh,
				    "Unknown protocol '%s'. "
				    "Valid: ble, lora, thread, fake",
				    argv[1]);
			return -EINVAL;
		}
	}

	struct remote_scan_ctrl_event evt = {
		.action = REMOTE_SCAN_STOP,
		.proto = proto,
	};
	int rc = zbus_chan_pub(&remote_scan_ctrl_chan, &evt, K_MSEC(100));

	if (rc != 0) {
		shell_error(sh, "Failed to publish scan stop: %d", rc);
		return rc;
	}

	if (proto == REMOTE_TRANSPORT_PROTO_UNKNOWN) {
		shell_print(sh, "Scan stopped on all transports");
	} else {
		shell_print(sh, "Scan stopped on %s", proto_to_str(proto));
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * remote_sensor transports — list linked-in transport adapters
 * -------------------------------------------------------------------------- */

static int cmd_transports(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int count = 0;

	shell_print(sh, "Registered transports:");
	STRUCT_SECTION_FOREACH(remote_transport, t)
	{
		shell_print(sh, "  %-8s proto=%d caps=0x%02x%s%s", t->name, (int)t->proto, t->caps,
			    (t->caps & REMOTE_TRANSPORT_CAP_SCAN) ? " SCAN" : "",
			    (t->caps & REMOTE_TRANSPORT_CAP_TRIGGER) ? " TRIGGER" : "");
		count++;
	}
	if (count == 0) {
		shell_print(sh, "  (none — enable a protocol adapter via Kconfig)");
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * Command tree
 * -------------------------------------------------------------------------- */

SHELL_STATIC_SUBCMD_SET_CREATE(scan_cmds,
			       SHELL_CMD_ARG(start, NULL,
					     "Start scanning [ble|lora|thread|fake] (empty = all)",
					     cmd_scan_start, 1, 1),
			       SHELL_CMD_ARG(stop, NULL,
					     "Stop scanning [ble|lora|thread|fake] (empty = all)",
					     cmd_scan_stop, 1, 1),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(remote_sensor_cmds,
			       SHELL_CMD(list, NULL, "List registered remote sensors", cmd_list),
			       SHELL_CMD(scan, &scan_cmds, "Scan control sub-commands", NULL),
			       SHELL_CMD(transports, NULL, "List linked-in transport adapters",
					 cmd_transports),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(remote_sensor, &remote_sensor_cmds, "Remote sensor management", NULL);
