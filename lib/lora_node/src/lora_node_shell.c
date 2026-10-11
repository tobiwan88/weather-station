/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <lora_node/lora_node.h>

static int cmd_provision(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long id = strtoul(argv[1], &end, 0);

	if (*argv[1] == '\0' || *end != '\0' || id < 1 || id > UINT16_MAX) {
		shell_error(sh, "node_id must be 1..65535");
		return -EINVAL;
	}
	if (strlen(argv[2]) != 32) {
		shell_error(sh, "key must be 32 hex chars");
		return -EINVAL;
	}

	uint8_t key[16];

	if (hex2bin(argv[2], 32, key, sizeof(key)) != sizeof(key)) {
		shell_error(sh, "key is not valid hex");
		return -EINVAL;
	}

	int ret = lora_node_save_session((uint16_t)id, key);

	memset(key, 0, sizeof(key));
	if (ret < 0) {
		shell_error(sh, "save failed: %d", ret);
		return ret;
	}
	shell_print(sh, "node %lu provisioned", id);
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint16_t id = lora_node_get_id();

	shell_print(sh, "node id: %u", id);
	shell_print(sh, "paired: %s", id != 0 ? "yes" : "no");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(lora_node_cmds,
			       SHELL_CMD_ARG(provision, NULL, "provision <node_id> <32-hex-key>",
					     cmd_provision, 3, 0),
			       SHELL_CMD(status, NULL, "Show node id, paired state, next seq",
					 cmd_status),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(lora_node, &lora_node_cmds, "LoRa node provisioning", NULL);
