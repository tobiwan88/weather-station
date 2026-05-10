/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_radio
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <sensor_event/sensor_event.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "lora_radio_internal.h"
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>

#include <lora_radio/lora_session.h>

#include <lora_radio/lora_chan.h>

#include <lora_radio/lora_radio_ops.h>

/* --------------------------------------------------------------------------
 * Radio ops selection
 * -------------------------------------------------------------------------- */
#ifdef CONFIG_LORA_RADIO_FAKE
extern const struct lora_radio_ops lora_fake_radio_ops;
const struct lora_radio_ops *lora_radio_ops = &lora_fake_radio_ops;
#elif CONFIG_LORA_RADIO_DRV_SX1262
extern const struct lora_radio_ops lora_sx1262_radio_ops;
const struct lora_radio_ops *lora_radio_ops = &lora_sx1262_radio_ops;
#else
#	error "No LoRa radio driver selected (CONFIG_LORA_RADIO_FAKE or CONFIG_LORA_RADIO_DRV_SX1262)"
#endif

/* --------------------------------------------------------------------------
 * Transport registration (iterable section metadata)
 * proto = 2 = REMOTE_TRANSPORT_PROTO_LORA, caps = BIT(0) = SCAN
 * -------------------------------------------------------------------------- */
REMOTE_TRANSPORT_REGISTER(lora_transport, {
						  .name = "lora",
						  .proto = 2,
						  .caps = BIT(0),
					  });

/* --------------------------------------------------------------------------
 * RX thread
 * -------------------------------------------------------------------------- */
K_THREAD_STACK_DEFINE(lora_rx_stack, CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE);
static struct k_thread lora_rx_thread_data;

/* --------------------------------------------------------------------------
 * Periodic trigger timer
 * -------------------------------------------------------------------------- */
#if CONFIG_LORA_RADIO_AUTO_PUBLISH_MS > 0
static struct k_timer lora_trigger_timer;

static void lora_trigger_handler(struct k_timer *timer)
{
	(void)timer;
	const struct sensor_trigger_event evt = {
		.source = 2,
		.target_uid = 0,
	};
	zbus_chan_pub(&sensor_trigger_chan, &evt, K_NO_WAIT);
}
#endif

/* --------------------------------------------------------------------------
 * UID: (0x0200 << 16) | (node_id << 4) | (type & 0x0F)
 * -------------------------------------------------------------------------- */
uint32_t lora_radio_uid_from_node_id(uint8_t node_id, uint8_t type)
{
	return (0x0200UL << 16) | ((uint32_t)node_id << 4) | (type & 0x0F);
}

/* --------------------------------------------------------------------------
 * Publish decoded sensor data to sensor_event_chan
 * -------------------------------------------------------------------------- */
int lora_radio_publish_data(uint32_t uid, uint8_t type, int32_t q31)
{
	struct env_sensor_data evt = {
		.sensor_uid = uid,
		.type = (enum sensor_type)type,
		.q31_value = q31,
		.timestamp_ms = k_uptime_get(),
	};
	return zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);
}

/* --------------------------------------------------------------------------
 * RX thread — main loop
 * -------------------------------------------------------------------------- */
static void lora_rx_thread_fn(void *p1, void *p2, void *p3)
{
	(void)p1;
	(void)p2;
	(void)p3;

	uint8_t buf[LORA_MAX_PACKET_SF7];
	uint8_t payload[LORA_MAX_PAYLOAD_SF7];
	struct lora_l2_header hdr;
	uint8_t payload_len;

	while (1) {
		int ret = lora_radio_ops->rx(buf, sizeof(buf), K_FOREVER);
		if (ret < 0) {
			continue;
		}
		uint8_t pkt_len = (uint8_t)ret;

		memcpy(&hdr, buf, sizeof(hdr));
		uint16_t src_node = (uint16_t)hdr.src_node[0] | ((uint16_t)hdr.src_node[1] << 8);
		struct lora_session *s = lora_session_get(src_node);
		const uint8_t *key = s ? s->session_key : NULL;

		/* Publish link diagnostics */
		struct lora_link_event link_evt = {
			.node_id = src_node,
			.rssi = lora_radio_ops->rssi(),
			.snr = (uint8_t)lora_radio_ops->snr(),
			.seq_num = (uint16_t)hdr.seq_num[0] | ((uint16_t)hdr.seq_num[1] << 8),
			.crc_errors = 0,
		};
		zbus_chan_pub(&lora_link_chan, &link_evt, K_NO_WAIT);

		/* Decode */
		ret = lora_packet_decode(buf, pkt_len, key, &hdr, payload, &payload_len);
		if (ret < 0) {
			LOG_WRN("packet decode failed from 0x%04x: %d", src_node, ret);
			continue;
		}

		if (s) {
			s->last_seq_rx = (uint16_t)hdr.seq_num[0] | ((uint16_t)hdr.seq_num[1] << 8);
			s->last_rx_ms = k_uptime_get();
		}

		switch (lora_frame_type(hdr.type_ver)) {
		case LORA_FRAME_SENSOR_DATA:
			lora_handle_sensor_data(src_node, payload, payload_len);
			break;
		case LORA_FRAME_RPC_CMD:
			lora_handle_rpc_cmd(src_node, payload, payload_len);
			break;
		case LORA_FRAME_PROV_BEACON:
			lora_handle_prov_beacon(&hdr, payload, payload_len);
			break;
#ifdef CONFIG_LORA_RADIO_FOTA
		case LORA_FRAME_FOTA_CHUNK:
			lora_handle_fota_chunk(src_node, payload, payload_len);
			break;
#endif
		default:
			LOG_DBG("unhandled frame type 0x%x from 0x%04x",
				lora_frame_type(hdr.type_ver), src_node);
			break;
		}
	}
}

/* --------------------------------------------------------------------------
 * SYS_INIT
 * -------------------------------------------------------------------------- */
static int lora_radio_init(void)
{
	lora_session_init();
	lora_session_restore();

	if (lora_radio_ops->init) {
		lora_radio_ops->init();
	}

	k_thread_create(&lora_rx_thread_data, lora_rx_stack, CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE,
			lora_rx_thread_fn, NULL, NULL, NULL, CONFIG_LORA_RADIO_RX_THREAD_PRIORITY,
			0, K_NO_WAIT);
	k_thread_name_set(&lora_rx_thread_data, "lora_rx");

#if CONFIG_LORA_RADIO_AUTO_PUBLISH_MS > 0
	k_timer_init(&lora_trigger_timer, lora_trigger_handler, NULL);
	k_timer_start(&lora_trigger_timer, K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS),
		      K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS));
#endif

	LOG_INF("LoRa radio initialized");
	return 0;
}

SYS_INIT(lora_radio_init, APPLICATION, 80);
