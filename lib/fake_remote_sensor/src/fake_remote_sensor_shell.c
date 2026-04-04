/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file fake_remote_sensor_shell.c
 * @brief Shell commands for the fake remote sensor testing stub.
 */

#include <zephyr/shell/shell.h>

#include <fake_remote_sensor/fake_remote_sensor.h>

static int cmd_announce(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int rc = fake_remote_sensor_announce();

	if (rc != 0) {
		shell_error(sh, "announce failed: %d", rc);
	} else {
		shell_print(sh, "Discovery events published for fake node(s)");
	}
	return rc;
}

static int cmd_publish(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	fake_remote_sensor_publish_all();
	shell_print(sh, "Published measurements for registered fake nodes");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(fake_remote_cmds,
	SHELL_CMD(announce, NULL,
		"Publish discovery events for all fake nodes",
		cmd_announce),
	SHELL_CMD(publish, NULL,
		"Publish one synthetic measurement from all registered nodes",
		cmd_publish),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(fake_remote, &fake_remote_cmds,
		   "Fake remote sensor control", NULL);
