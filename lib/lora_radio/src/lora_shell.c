/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

/* Parse an unsigned number in @p base with end-pointer and range checks */
static int parse_uint(const struct shell *sh, const char *arg, int base, unsigned long min,
		      unsigned long max, const char *what, unsigned long *out)
{
	char *end;
	unsigned long v;

	errno = 0;
	v = strtoul(arg, &end, base);
	if (*arg == '\0' || *end != '\0' || errno != 0 || v < min || v > max) {
		shell_error(sh, "%s must be %lu..%lu", what, min, max);
		return -EINVAL;
	}
	*out = v;
	return 0;
}

/* Node ids are 1..255 (decimal, or 0x-prefixed hex; same as provision/forget) */
static int parse_node_id(const struct shell *sh, const char *arg, uint16_t *out)
{
	unsigned long id;
	int ret = parse_uint(sh, arg, 0, 1, 0x00FF, "node_id", &id);

	if (ret == 0) {
		*out = (uint16_t)id;
	}
	return ret;
}

static int cmd_lora_rpc(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_print(sh, "Usage: lora rpc <node_id_hex> <cmd_id_hex> [params_hex]");
		return -EINVAL;
	}
	unsigned long id;
	unsigned long cmd;
	uint8_t params[64] = {0};
	uint8_t param_len = 0;
	int ret = parse_uint(sh, argv[2], 16, 1, 0x00FF, "node_id (hex)", &id);

	if (ret < 0) {
		return ret;
	}
	ret = parse_uint(sh, argv[3], 16, 0, 0xFF, "cmd_id (hex)", &cmd);
	if (ret < 0) {
		return ret;
	}
	uint16_t node_id = (uint16_t)id;
	uint8_t cmd_id = (uint8_t)cmd;

	if (argc > 4) {
		size_t hex_len = strlen(argv[4]);

		if (hex_len == 0 || hex_len % 2 != 0 || hex_len / 2 > sizeof(params)) {
			shell_error(sh, "params must be 1..%zu bytes of hex", sizeof(params));
			return -EINVAL;
		}
		param_len = hex2bin(argv[4], hex_len, params, sizeof(params));
		if (param_len != hex_len / 2) {
			shell_error(sh, "params is not valid hex");
			return -EINVAL;
		}
	}
	ret = lora_radio_rpc_send(node_id, cmd_id, params, param_len);
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
		shell_print(sh, "  0x%04x  %-10s %-8u %-8u %lld", id, state, s->last_seq_rx,
			    s->last_seq_tx, (long long)s->last_rx_ms);
	}
	return 0;
}

static int cmd_lora_provision(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node_id;
	uint8_t key[16];
	char hex[2 * sizeof(key) + 1];
	int ret = parse_node_id(sh, argv[1], &node_id);

	if (ret < 0) {
		return ret;
	}
#ifdef CONFIG_CSPRNG_ENABLED
	ret = sys_csrand_get(key, sizeof(key));
	if (ret < 0) {
		shell_error(sh, "CSPRNG failed: %d", ret);
		return ret;
	}
#else
	shell_warn(sh, "CSPRNG not available, using non-cryptographic RNG");
	sys_rand_get(key, sizeof(key));
#endif
	/* Re-provisioning replaces the existing key */
	if (lora_session_get(node_id)) {
		lora_session_remove(node_id);
	}
	if (!lora_session_add(node_id, key)) {
		memset(key, 0, sizeof(key));
		shell_error(sh, "session table full");
		return -ENOMEM;
	}
	lora_session_persist();

	bin2hex(key, sizeof(key), hex, sizeof(hex));
	memset(key, 0, sizeof(key));
	shell_print(sh, "node %u key: %s", node_id, hex);
	shell_print(sh, "Shown once. On the node run: lora_node provision %u <key>", node_id);
	memset(hex, 0, sizeof(hex));
	return 0;
}

static int cmd_lora_forget(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node_id;
	char name[24];
	int ret = parse_node_id(sh, argv[1], &node_id);

	if (ret < 0) {
		return ret;
	}
	if (!lora_session_get(node_id)) {
		shell_error(sh, "no session for node %u", node_id);
		return -ENOENT;
	}
	lora_session_remove(node_id);
	/* Drop the persisted key too, otherwise it would be restored on reboot */
	snprintk(name, sizeof(name), "lora/%04x/key", node_id);
	ret = settings_delete(name);
	if (ret < 0) {
		shell_warn(sh, "persisted key delete failed: %d", ret);
	}
	lora_session_persist();
	shell_print(sh, "node %u forgotten", node_id);
	return 0;
}

static int cmd_lora_sessions(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	uint16_t count = 0;

	shell_print(sh, "%-8s %-12s %s", "Node ID", "Last RX seq", "Replay drops");
	for (uint16_t id = 0x0001; id <= 0x00FF; id++) {
		struct lora_session *s = lora_session_get(id);

		if (!s) {
			continue;
		}
		shell_print(sh, "%-8u %-12u %u", id, s->last_seq_rx, s->replay_drops);
		count++;
	}
	shell_print(sh, "%u session(s)", count);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	lora_cmds, SHELL_CMD(rpc, NULL, "rpc <node_id_hex> <cmd_hex> [params_hex]", cmd_lora_rpc),
	SHELL_CMD(status, NULL, "Show paired nodes and session state", cmd_lora_status),
	SHELL_CMD_ARG(provision, NULL, "provision <node_id>: generate a node key",
		      cmd_lora_provision, 2, 0),
	SHELL_CMD_ARG(forget, NULL, "forget <node_id>", cmd_lora_forget, 2, 0),
	SHELL_CMD(sessions, NULL, "List sessions (no keys)", cmd_lora_sessions),
	SHELL_CMD_ARG(unpair, NULL, "unpair <node_id>: alias of forget", cmd_lora_forget, 2, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(lora, &lora_cmds, "LoRa radio management", NULL);
