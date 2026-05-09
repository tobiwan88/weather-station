/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/hexdump.h>

#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

static int cmd_lora_rpc(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_print(sh, "Usage: lora rpc <node_id_hex> <cmd_id_hex> [params_hex]");
		return -EINVAL;
	}
	uint16_t node_id = (uint16_t)strtol(argv[2], NULL, 16);
	uint8_t cmd_id = (uint8_t)strtol(argv[3], NULL, 16);
	uint8_t params[64] = {0};
	uint8_t param_len = 0;
	if (argc > 4) {
		param_len = hex2bin(argv[4], strlen(argv[4]), params, sizeof(params));
	}
	int ret = lora_radio_rpc_send(node_id, cmd_id, params, param_len);
	shell_print(sh, "RPC 0x%02x -> node 0x%04x: %s", cmd_id, node_id,
		    ret == 0 ? "OK" : strerror(-ret));
	return ret;
}

static int cmd_lora_status(const struct shell *sh, size_t argc, char **argv)
{
	(void)argc;
	(void)argv;
	shell_print(sh, "LoRa radio sessions:");
	shell_print(sh, "  %-8s %-10s %-8s %-8s %s", "Node ID", "State", "Seq(RX)", "Seq(TX)",
		    "Last RX");
	for (uint16_t id = 0x0001; id <= 0x00FF; id++) {
		struct lora_session *s = lora_session_get(id);
		if (!s) {
			continue;
		}
		const char *state;
		switch (s->state) {
		case LORA_SESSION_UNPAIRED:
			state = "unpaired";
			break;
		case LORA_SESSION_PAIRING:
			state = "pairing";
			break;
		case LORA_SESSION_PAIRED:
			state = "paired";
			break;
		case LORA_SESSION_EXPIRED:
			state = "expired";
			break;
		default:
			state = "?";
			break;
		}
		shell_print(sh, "  0x%04x  %-10s %-8d %-8d %lld", id, state, s->last_seq_rx,
			    s->last_seq_tx, (long long)s->last_rx_ms);
	}
	return 0;
}

static int cmd_lora_unpair(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Usage: lora unpair <node_id_hex>");
		return -EINVAL;
	}
	uint16_t node_id = (uint16_t)strtol(argv[1], NULL, 16);
	lora_session_remove(node_id);
	shell_print(sh, "node 0x%04x unpaired", node_id);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	lora_cmds, SHELL_CMD(rpc, NULL, "rpc <node_id_hex> <cmd_hex> [params_hex]", cmd_lora_rpc),
	SHELL_CMD(status, NULL, "Show paired nodes and session state", cmd_lora_status),
	SHELL_CMD(unpair, NULL, "unpair <node_id_hex>", cmd_lora_unpair), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(lora, &lora_cmds, "LoRa radio management", NULL);
