/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_rpc
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_REBOOT
#	include <zephyr/sys/reboot.h>
#endif

#include "lora_radio_internal.h"
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

typedef int (*rpc_handler_t)(uint16_t src_node, uint8_t cmd_id, const uint8_t *params,
			     uint8_t param_len, uint8_t *resp_data, uint8_t *resp_len);

static int rpc_ping(uint16_t src, uint8_t id, const uint8_t *p, uint8_t plen, uint8_t *r,
		    uint8_t *rlen)
{
	(void)src;
	(void)id;
	(void)p;
	(void)plen;
	int64_t uptime = k_uptime_get();
	memcpy(r, &uptime, sizeof(uptime));
	*rlen = sizeof(uptime);
	return 0;
}

static int rpc_get_version(uint16_t src, uint8_t id, const uint8_t *p, uint8_t plen, uint8_t *r,
			   uint8_t *rlen)
{
	(void)src;
	(void)id;
	(void)p;
	(void)plen;
	const char *ver = STRINGIFY(CONFIG_APP_VERSION);
	uint8_t vlen = strlen(ver);
	memcpy(r, ver, vlen);
	*rlen = vlen;
	return 0;
}

#ifdef CONFIG_REBOOT
static void lora_reboot_fn(struct k_work *work)
{
	(void)work;
	LOG_INF("rebooting via RPC");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(lora_reboot_work, lora_reboot_fn);

static int rpc_reboot(uint16_t src, uint8_t id, const uint8_t *p, uint8_t plen, uint8_t *r,
		      uint8_t *rlen)
{
	(void)src;
	(void)id;
	(void)p;
	(void)plen;
	*rlen = 0;
	k_work_schedule(&lora_reboot_work, K_MSEC(100));
	return 0;
}
#else
static int rpc_reboot(uint16_t src, uint8_t id, const uint8_t *p, uint8_t plen, uint8_t *r,
		      uint8_t *rlen)
{
	(void)src;
	(void)id;
	(void)p;
	(void)plen;
	(void)r;
	(void)rlen;
	LOG_WRN("reboot via RPC not supported on this platform");
	return -ENOTSUP;
}
#endif

static const struct {
	uint8_t cmd_id;
	rpc_handler_t handler;
} rpc_dispatch[] = {
	{LORA_RPC_PING, rpc_ping},
	{LORA_RPC_GET_VERSION, rpc_get_version},
	{LORA_RPC_REBOOT, rpc_reboot},
};

int lora_handle_rpc_cmd(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 2) {
		return -EINVAL;
	}

	uint8_t cmd_id = payload[0];
	uint8_t param_len = payload[1];
	const uint8_t *params = payload + 2;

	rpc_handler_t handler = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(rpc_dispatch); i++) {
		if (rpc_dispatch[i].cmd_id == cmd_id) {
			handler = rpc_dispatch[i].handler;
			break;
		}
	}
	if (!handler) {
		LOG_WRN("unknown RPC cmd 0x%02x from node 0x%04x", cmd_id, src_node);
		return -ENOTSUP;
	}

	uint8_t resp_data[128];
	uint8_t resp_len = 0;
	int status = handler(src_node, cmd_id, params, param_len, resp_data, &resp_len);

	if (status == 0 && resp_len > sizeof(resp_data)) {
		resp_len = sizeof(resp_data);
	}

	uint8_t rpc_rsp_buf[130];
	rpc_rsp_buf[0] = cmd_id;
	rpc_rsp_buf[1] = (status < 0) ? (uint8_t)(-status) : 0;
	if (resp_len > 0) {
		memcpy(rpc_rsp_buf + 2, resp_data, resp_len);
	}
	uint8_t total_resp_len = resp_len + 2;

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_RPC_RESP << 4) | 0x01;
	hdr.flags = LORA_FLAG_ENCRYPTED;
	hdr.dst_node[0] = (uint8_t)(src_node & 0xFF);
	hdr.dst_node[1] = (uint8_t)((src_node >> 8) & 0xFF);

	struct lora_session *s = lora_session_get(src_node);
	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;

	lora_packet_encode(&hdr, rpc_rsp_buf, total_resp_len, s ? s->session_key : NULL, tx_buf,
			   &tx_len);
	lora_send(lora_radio_dev, tx_buf, tx_len);
	return 0;
}

int lora_radio_rpc_send(uint16_t node_id, uint8_t cmd_id, const uint8_t *params, uint8_t param_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	uint8_t payload[2 + (param_len > 128 ? 128 : param_len)];
	payload[0] = cmd_id;
	payload[1] = param_len;
	if (param_len > 0) {
		memcpy(payload + 2, params, MIN(param_len, 128));
	}

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_RPC_CMD << 4) | 0x01;
	hdr.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED;
	hdr.dst_node[0] = (uint8_t)(node_id & 0xFF);
	hdr.dst_node[1] = (uint8_t)((node_id >> 8) & 0xFF);
	hdr.seq_num[0] = (uint8_t)(s->last_seq_tx & 0xFF);
	hdr.seq_num[1] = (uint8_t)((s->last_seq_tx >> 8) & 0xFF);
	s->last_seq_tx++;

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	uint8_t tx_len;
	int ret =
		lora_packet_encode(&hdr, payload, sizeof(payload), s->session_key, tx_buf, &tx_len);
	if (ret < 0) {
		return ret;
	}
	return lora_send(lora_radio_dev, tx_buf, tx_len);
}
