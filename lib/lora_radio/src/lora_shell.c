/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <lora_radio/lora_radio.h>

LOG_MODULE_DECLARE(lora_radio);

static int cmd_lora_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "LoRa radio subsystem");
	shell_print(sh, "  sessions: max=%d", CONFIG_LORA_RADIO_SESSION_MAX);
	shell_print(sh, "  sf=%d bw=%d", CONFIG_LORA_RADIO_DEFAULT_SF,
		    CONFIG_LORA_RADIO_DEFAULT_BW);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(lora_cmds,
			       SHELL_CMD(status, NULL, "Show radio and session state",
					 cmd_lora_status),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(lora, &lora_cmds, "LoRa radio control", NULL);
