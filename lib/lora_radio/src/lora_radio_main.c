/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_radio.h>
#include <sensor_event/sensor_event.h>

LOG_MODULE_REGISTER(lora_radio, CONFIG_LORA_RADIO_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Radio hardware operations
 * --------------------------------------------------------------------------
 * Selected at compile time based on Kconfig. The actual driver
 * implementations (fake / SX1262) are provided in separate source files.
 */

struct lora_radio_ops {
	int (*init)(void);
	int (*send)(const uint8_t *buf, size_t len);
	int (*recv)(uint8_t *buf, size_t *len, int16_t *rssi, int8_t *snr);
	int (*sleep)(void);
};

#ifdef CONFIG_LORA_RADIO_FAKE
extern const struct lora_radio_ops lora_fake_ops;
#	define LORA_RADIO_OPS (&lora_fake_ops)
#elif CONFIG_LORA_RADIO_DRV_SX1262
extern const struct lora_radio_ops lora_sx1262_ops;
#	define LORA_RADIO_OPS (&lora_sx1262_ops)
#else
#	error "No LoRa radio driver selected (CONFIG_LORA_RADIO_FAKE or CONFIG_LORA_RADIO_DRV_SX1262)"
#endif

/* --------------------------------------------------------------------------
 * Transport registration (iterable section metadata)
 * --------------------------------------------------------------------------
 * proto = 2 = REMOTE_TRANSPORT_PROTO_LORA
 * caps  = BIT(0) = REMOTE_TRANSPORT_CAP_SCAN
 */

REMOTE_TRANSPORT_REGISTER(lora_transport, {
						  .name = "lora",
						  .proto = 2,
						  .caps = BIT(0),
					  });

/* --------------------------------------------------------------------------
 * RX thread
 * -------------------------------------------------------------------------- */

K_THREAD_STACK_DEFINE(lora_rx_stack, CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE);
static struct k_thread lora_rx_thread;

/* --------------------------------------------------------------------------
 * Periodic trigger timer
 * -------------------------------------------------------------------------- */

#if CONFIG_LORA_RADIO_AUTO_PUBLISH_MS > 0
static struct k_timer lora_auto_timer;

static void lora_auto_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* TODO: publish trigger to all paired nodes */
}
#endif

/* --------------------------------------------------------------------------
 * Forward declarations for protocol handlers
 * Defined in separate handler source files.
 * -------------------------------------------------------------------------- */

void lora_handle_sensor_data(const struct lora_l2_header *hdr, const uint8_t *payload, size_t len);
void lora_handle_rpc_cmd(const struct lora_l2_header *hdr, const uint8_t *payload, size_t len);
void lora_handle_prov_beacon(const struct lora_l2_header *hdr, const uint8_t *payload, size_t len);

#ifdef CONFIG_LORA_RADIO_FOTA
void lora_handle_fota_chunk(const struct lora_l2_header *hdr, const uint8_t *payload, size_t len);
#endif

/* --------------------------------------------------------------------------
 * Packet decode (forward declaration — defined in lora_packet.c)
 * -------------------------------------------------------------------------- */

int lora_packet_decode(const uint8_t *buf, size_t len, struct lora_l2_header *hdr,
		       const uint8_t **payload, size_t *payload_len);

/* --------------------------------------------------------------------------
 * Public API: uid derivation
 * --------------------------------------------------------------------------
 * uid = (0x0200 << 16) | (node_id << 4) | (type & 0x0F)
 */

uint32_t lora_radio_uid_from_node_id(uint8_t node_id, enum sensor_type type)
{
	return (0x0200UL << 16) | ((uint32_t)node_id << 4) | ((uint32_t)type & 0x0F);
}

/* --------------------------------------------------------------------------
 * Public API: publish data to sensor_event_chan
 * -------------------------------------------------------------------------- */

int lora_radio_publish_data(uint32_t uid, enum sensor_type type, int32_t q31_value)
{
	struct env_sensor_data evt = {
		.sensor_uid = uid,
		.type = type,
		.q31_value = q31_value,
		.timestamp_ms = k_uptime_get(),
	};

	return zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT);
}

/* --------------------------------------------------------------------------
 * Public API: send RPC command (stub — implemented in later task)
 * -------------------------------------------------------------------------- */

int lora_radio_rpc_send(uint8_t node_id, uint8_t cmd_id, const uint8_t *params, size_t params_len)
{
	ARG_UNUSED(node_id);
	ARG_UNUSED(cmd_id);
	ARG_UNUSED(params);
	ARG_UNUSED(params_len);

	return -ENOSYS;
}

/* --------------------------------------------------------------------------
 * RX thread main loop
 * -------------------------------------------------------------------------- */

static void lora_rx_thread_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint8_t buf[LORA_MAX_PACKET_SF7];

	while (1) {
		size_t len = sizeof(buf);
		int16_t rssi;
		int8_t snr;

		int ret = LORA_RADIO_OPS->recv(buf, &len, &rssi, &snr);
		if (ret < 0) {
			k_sleep(K_MSEC(100));
			continue;
		}

		if (len < sizeof(struct lora_l2_header)) {
			LOG_WRN("short frame: %zu bytes", len);
			continue;
		}

		/* Publish link diagnostics */
		const struct lora_l2_header *hdr = (const struct lora_l2_header *)buf;
		struct lora_link_info link = {
			.rssi = rssi,
			.snr = snr,
			.src_node = hdr->src_node[0],
			.len = (uint8_t)len,
		};
		zbus_chan_pub(&lora_link_chan, &link, K_NO_WAIT);

		/* Decode and dispatch */
		struct lora_l2_header decoded_hdr;
		const uint8_t *payload;
		size_t payload_len;

		ret = lora_packet_decode(buf, len, &decoded_hdr, &payload, &payload_len);
		if (ret < 0) {
			LOG_WRN("packet decode failed: %d", ret);
			continue;
		}

		switch (lora_frame_type(decoded_hdr.type_ver)) {
		case LORA_FRAME_SENSOR_DATA:
			lora_handle_sensor_data(&decoded_hdr, payload, payload_len);
			break;
		case LORA_FRAME_RPC_CMD:
			lora_handle_rpc_cmd(&decoded_hdr, payload, payload_len);
			break;
		case LORA_FRAME_PROV_BEACON:
			lora_handle_prov_beacon(&decoded_hdr, payload, payload_len);
			break;
#ifdef CONFIG_LORA_RADIO_FOTA
		case LORA_FRAME_FOTA_CHUNK:
			lora_handle_fota_chunk(&decoded_hdr, payload, payload_len);
			break;
#endif
		default:
			LOG_DBG("unhandled frame type 0x%x", lora_frame_type(decoded_hdr.type_ver));
			break;
		}
	}
}

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 80
 * -------------------------------------------------------------------------- */

static int lora_radio_init(void)
{
	/* Initialize session manager */
	extern int lora_session_init(void);
	extern int lora_session_restore(void);

	int ret = lora_session_init();
	if (ret != 0) {
		LOG_ERR("session init failed: %d", ret);
		return ret;
	}

	ret = lora_session_restore();
	if (ret != 0) {
		LOG_WRN("session restore failed: %d", ret);
	}

	/* Initialize radio hardware */
	ret = LORA_RADIO_OPS->init();
	if (ret != 0) {
		LOG_ERR("radio init failed: %d", ret);
		return ret;
	}

	/* Start RX thread */
	k_thread_create(&lora_rx_thread, lora_rx_stack, CONFIG_LORA_RADIO_RX_THREAD_STACK_SIZE,
			lora_rx_thread_fn, NULL, NULL, NULL, CONFIG_LORA_RADIO_RX_THREAD_PRIORITY,
			0, K_NO_WAIT);
	k_thread_name_set(&lora_rx_thread, "lora_rx");

	/* Start auto-publish timer */
#if CONFIG_LORA_RADIO_AUTO_PUBLISH_MS > 0
	k_timer_init(&lora_auto_timer, lora_auto_timer_cb, NULL);
	k_timer_start(&lora_auto_timer, K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS),
		      K_MSEC(CONFIG_LORA_RADIO_AUTO_PUBLISH_MS));
#endif

	LOG_INF("lora_radio initialized (proto=2, caps=0x%02x)", BIT(0));
	return 0;
}

SYS_INIT(lora_radio_init, APPLICATION, 80);
