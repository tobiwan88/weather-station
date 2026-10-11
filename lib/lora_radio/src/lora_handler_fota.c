/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME lora_fota
#define LOG_LEVEL       CONFIG_LORA_RADIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "lora_radio_internal.h"
#include <io_stream/io_stream.h>
#include <lora_radio/lora_chan.h>
#include <lora_radio/lora_frame.h>
#include <lora_radio/lora_pending.h>
#include <lora_radio/lora_radio.h>
#include <lora_radio/lora_session.h>

#if defined(CONFIG_IO_STREAM)
#	if defined(CONFIG_IO_STREAM_FLASH)
#		include <zephyr/storage/flash_map.h>
#	endif

/* --------------------------------------------------------------------------
 * FOTA sender state machine
 * -------------------------------------------------------------------------- */

BUILD_ASSERT(sizeof(struct lora_fota_chunk) + CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE + LORA_OVERHEAD <=
		     LORA_MAX_PACKET_SF7,
	     "FOTA chunk plus frame overhead must fit one LoRa packet");

enum fota_sender_state {
	FOTA_STATE_IDLE,
	FOTA_STATE_SENDING,
	FOTA_STATE_COOLDOWN,
	FOTA_STATE_FAILED,
};

static struct {
	enum fota_sender_state state;
	uint16_t target_node_id;
	uint32_t image_size;
	uint32_t bytes_sent;
	uint32_t bytes_acked;
	uint32_t pending_expected_offset; /* stored by ACK handler for callback access */
	uint32_t inflight_offset;         /* offset of the single in-flight chunk (ACK matching) */
	uint8_t fota_mode;
	uint8_t window_size;
	uint8_t window_sent;
	uint8_t consecutive_timeouts;
	uint8_t chunk_data[CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE];
	struct k_work_delayable work;
	struct io_stream src_stream;
} fota_sender;

/* --------------------------------------------------------------------------
 * FOTA chunk completion callback — invoked from pending workqueue
 * -------------------------------------------------------------------------- */

static void fota_chunk_cb(uint16_t node_id, int status, void *user_data)
{
	(void)node_id;
	(void)user_data;

	if (status == 0) {
		fota_sender.bytes_acked = fota_sender.pending_expected_offset;
		fota_sender.window_sent = MAX(0, fota_sender.window_sent - 1);
		fota_sender.consecutive_timeouts = 0;

		/* Advance window if space available */
		if (fota_sender.state == FOTA_STATE_SENDING &&
		    fota_sender.window_sent < fota_sender.window_size) {
			k_work_schedule_for_queue(&pending_workq, &fota_sender.work, K_MSEC(50));
		}
	} else {
		fota_sender.consecutive_timeouts++;
		if (fota_sender.consecutive_timeouts >= 3) {
			fota_sender.state = FOTA_STATE_COOLDOWN;
			k_work_schedule_for_queue(&pending_workq, &fota_sender.work,
						  K_SECONDS(CONFIG_LORA_RADIO_FOTA_COOLDOWN_S));
		}
	}
}

/* --------------------------------------------------------------------------
 * Send one chunk
 * -------------------------------------------------------------------------- */

static int fota_send_chunk(uint16_t node_id, uint32_t offset, const uint8_t *data, uint8_t data_len)
{
	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		return -EINVAL;
	}

	/* struct lora_fota_chunk ends in a flexible array member, so it cannot hold
	 * the data: build the payload in a buffer sized for header + chunk.
	 */
	uint8_t chunk[sizeof(struct lora_fota_chunk) + CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE];

	if (data_len > CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE) {
		return -EMSGSIZE;
	}
	sys_put_le32(offset, chunk);
	memcpy(&chunk[sizeof(struct lora_fota_chunk)], data, data_len);

	struct lora_l2_header hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.type_ver = (LORA_FRAME_FOTA_CHUNK << 4) | 0x01;
	hdr.flags = LORA_FLAG_ACK_REQ | LORA_FLAG_ENCRYPTED | LORA_FLAG_DIR_DOWN;
	lora_hdr_dst_set(&hdr, node_id);

	/* Fresh seq per frame: the nonce is derived from it */
	uint32_t seq;
	int ret = lora_session_next_tx_seq(&seq);

	if (ret < 0) {
		return ret;
	}
	lora_hdr_seq_set(&hdr, seq);

	uint8_t tx_buf[LORA_MAX_PACKET_SF7];
	size_t tx_len;

	ret = lora_packet_encode(&hdr, chunk, sizeof(struct lora_fota_chunk) + data_len,
				 s->session_key, tx_buf, sizeof(tx_buf), &tx_len);
	if (ret < 0) {
		return ret;
	}

	/* Set before sending so a fast ACK finds it; restored if the slot is busy */
	uint32_t prev_offset = fota_sender.inflight_offset;

	fota_sender.inflight_offset = offset;
	ret = lora_pending_send_async(node_id, seq, LORA_FRAME_FOTA_CHUNK, tx_buf, tx_len,
				      CONFIG_LORA_RADIO_FOTA_RETRY_MS, fota_chunk_cb, NULL);
	if (ret < 0) {
		fota_sender.inflight_offset = prev_offset;
	}
	return ret;
}

/* --------------------------------------------------------------------------
 * Advance window — send next batch of chunks
 * -------------------------------------------------------------------------- */

static void fota_advance_window(struct k_work *work);

static void fota_send_next_chunk(struct k_work *work)
{
	(void)work;

	if (fota_sender.state != FOTA_STATE_SENDING) {
		return;
	}

	if (fota_sender.bytes_sent >= fota_sender.image_size) {
		LOG_INF("FOTA complete for node 0x%04x (%u bytes)", fota_sender.target_node_id,
			fota_sender.bytes_acked);
		fota_sender.state = FOTA_STATE_IDLE;
		return;
	}

	if (fota_sender.window_sent >= fota_sender.window_size) {
		/* Window full — wait for ACKs, scheduler will call us again */
		return;
	}

	uint32_t offset = fota_sender.bytes_sent;
	uint32_t remaining = fota_sender.image_size - offset;
	uint8_t chunk_size = (uint8_t)MIN(remaining, (uint32_t)CONFIG_LORA_RADIO_FOTA_CHUNK_SIZE);

	int ret = io_stream_seek(&fota_sender.src_stream, (int32_t)offset, IO_STREAM_SEEK_SET);
	if (ret < 0) {
		LOG_ERR("FOTA stream seek failed at offset %u: %d", offset, ret);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}

	ssize_t bytes_read =
		io_stream_read(&fota_sender.src_stream, fota_sender.chunk_data, chunk_size);
	if (bytes_read < 0) {
		LOG_ERR("FOTA stream read failed at offset %u: %zd", offset, bytes_read);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}
	if (bytes_read == 0) {
		LOG_ERR("FOTA stream returned EOF at offset %u", offset);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}
	chunk_size = (uint8_t)bytes_read;

	ret = fota_send_chunk(fota_sender.target_node_id, offset, fota_sender.chunk_data,
			      chunk_size);
	if (ret < 0) {
		/* -EBUSY means the pending slot is still occupied (one chunk in flight).
		 * Don't fail — the ACK callback will schedule the next chunk.
		 */
		if (ret == -EBUSY) {
			return;
		}
		LOG_ERR("FOTA chunk send failed: %d", ret);
		fota_sender.state = FOTA_STATE_FAILED;
		return;
	}

	fota_sender.bytes_sent += chunk_size;
	fota_sender.window_sent++;
}

/* --------------------------------------------------------------------------
 * Work handler — manages window and cooldown
 * -------------------------------------------------------------------------- */

static void fota_advance_window(struct k_work *work)
{
	(void)work;

	switch (fota_sender.state) {
	case FOTA_STATE_SENDING:
		fota_send_next_chunk(work);
		break;
	case FOTA_STATE_COOLDOWN:
		if (fota_sender.consecutive_timeouts >= 3) {
			LOG_ERR("FOTA failed after cooldown for node 0x%04x",
				fota_sender.target_node_id);
			fota_sender.state = FOTA_STATE_FAILED;
		} else {
			fota_sender.consecutive_timeouts = 0;
			fota_sender.state = FOTA_STATE_SENDING;
			fota_send_next_chunk(work);
		}
		break;
	default:
		break;
	}
}

/* --------------------------------------------------------------------------
 * FOTA_START handler — called from lora_fota_chan subscriber
 * -------------------------------------------------------------------------- */

static void fota_start_handler(uint16_t node_id, uint32_t image_size, uint8_t fota_mode)
{
	if (fota_sender.state != FOTA_STATE_IDLE) {
		LOG_WRN("FOTA already in progress (state=%d)", fota_sender.state);
		return;
	}

	struct lora_session *s = lora_session_get(node_id);
	if (!s) {
		LOG_ERR("no session for node 0x%04x", node_id);
		return;
	}

	fota_sender.state = FOTA_STATE_SENDING;
	fota_sender.target_node_id = node_id;
	fota_sender.image_size = image_size;
	fota_sender.bytes_sent = 0;
	fota_sender.bytes_acked = 0;
	fota_sender.pending_expected_offset = 0;
	fota_sender.inflight_offset = 0;
	fota_sender.fota_mode = fota_mode;
	fota_sender.window_size = CONFIG_LORA_RADIO_FOTA_WINDOW;
	fota_sender.window_sent = 0;
	fota_sender.consecutive_timeouts = 0;

#	if defined(CONFIG_IO_STREAM_FLASH)
	int rc = io_stream_flash_init(&fota_sender.src_stream, PARTITION_ID(slot1_partition));
	if (rc < 0) {
		LOG_ERR("FOTA stream init failed: %d", rc);
		fota_sender.state = FOTA_STATE_IDLE;
		return;
	}
#	else
	LOG_ERR("FOTA sender requires CONFIG_IO_STREAM_FLASH");
	fota_sender.state = FOTA_STATE_IDLE;
	return;
#	endif

	LOG_INF("FOTA start for node 0x%04x (%u bytes, mode=0x%02x)", node_id, image_size,
		fota_mode);

	fota_send_next_chunk(NULL);

	/* Guard reschedule: if the first chunk never ACKs (e.g., radio failure),
	 * the sender would stall forever without this fallback.
	 */
	k_work_schedule_for_queue(&pending_workq, &fota_sender.work, K_MSEC(100));
}

/* --------------------------------------------------------------------------
 * FOTA_CANCEL handler
 * -------------------------------------------------------------------------- */

static void fota_cancel_handler(uint16_t node_id)
{
	(void)node_id;

	if (fota_sender.state == FOTA_STATE_IDLE) {
		return;
	}

	lora_pending_cancel(fota_sender.target_node_id);
	fota_sender.state = FOTA_STATE_IDLE;
	LOG_INF("FOTA cancelled for node 0x%04x", fota_sender.target_node_id);
}

/* --------------------------------------------------------------------------
 * FOTA_CHUNK_ACK handler — called from RX thread
 * -------------------------------------------------------------------------- */

int lora_handle_fota_chunk_ack(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < sizeof(struct lora_fota_chunk_ack)) {
		return -EINVAL;
	}

	const struct lora_fota_chunk_ack *ack = (const struct lora_fota_chunk_ack *)payload;

	uint32_t acked_offset;
	uint32_t expected_offset;

	memcpy(&acked_offset, ack->offset, sizeof(acked_offset));
	memcpy(&expected_offset, ack->expected_offset, sizeof(expected_offset));

	/* The ACK carries the receiver's own seq, and every (re)transmission of a
	 * chunk uses a new seq, so match on the chunk offset it echoes. Stale ACKs
	 * of an earlier chunk are ignored.
	 */
	if (fota_sender.state == FOTA_STATE_IDLE || src_node != fota_sender.target_node_id ||
	    acked_offset != fota_sender.inflight_offset) {
		LOG_DBG("ignoring FOTA ACK for offset %u", acked_offset);
		return 0;
	}

	/* Store expected_offset for the callback to read */
	fota_sender.pending_expected_offset = expected_offset;

	/* Complete the pending slot — callback (fota_chunk_cb) handles window advance */
	lora_pending_complete(src_node, LORA_FRAME_FOTA_CHUNK, ack->status != 0 ? -EIO : 0);

	return 0;
}

/* --------------------------------------------------------------------------
 * FOTA receiver — process incoming chunk (sensor node side)
 * -------------------------------------------------------------------------- */

static struct io_stream fota_rx_stream;
static uint32_t fota_rx_expected_offset;
static bool fota_rx_active;

int lora_handle_fota_chunk(uint16_t src_node, const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len < 5) {
		return -EINVAL;
	}

	uint32_t offset;

	memcpy(&offset, payload, sizeof(offset));
	const uint8_t *data = payload + 4;
	uint8_t data_len = payload_len - 4;
	int ret = 0;

	if (!fota_rx_active) {
#	if defined(CONFIG_IO_STREAM_FLASH)
		ret = io_stream_flash_init(&fota_rx_stream, PARTITION_ID(slot1_partition));
		if (ret < 0) {
			LOG_ERR("FOTA RX stream init failed: %d", ret);
			return ret;
		}
#	else
		LOG_ERR("FOTA receiver requires CONFIG_IO_STREAM_FLASH");
		return -ENODEV;
#	endif
		fota_rx_active = true;
		fota_rx_expected_offset = 0;
	}

	/* Ignore duplicate/out-of-order chunks */
	if (offset != fota_rx_expected_offset) {
		LOG_WRN("FOTA offset mismatch: expected %u, got %u", fota_rx_expected_offset,
			offset);
		ret = -EINVAL;
	} else {
		ssize_t written = io_stream_write(&fota_rx_stream, data, data_len);
		if (written < 0) {
			ret = (int)written;
			LOG_ERR("stream write failed at offset %u: %d", offset, ret);
		} else {
			fota_rx_expected_offset = offset + (uint32_t)written;
		}
	}

	{
		struct lora_fota_chunk_ack ack;

		memcpy(ack.offset, &offset, sizeof(ack.offset));
		memcpy(ack.expected_offset, &fota_rx_expected_offset, sizeof(ack.expected_offset));
		ack.status = (ret < 0) ? 1 : 0;

		struct lora_l2_header hdr;

		memset(&hdr, 0, sizeof(hdr));
		hdr.type_ver = (LORA_FRAME_FOTA_CHUNK_ACK << 4) | 0x01;
		hdr.flags = LORA_FLAG_DIR_DOWN;
		lora_hdr_dst_set(&hdr, src_node);

		struct lora_session *s = lora_session_get(src_node);
		uint8_t tx_buf[LORA_MAX_PACKET_SF7];
		size_t tx_len;
		uint32_t seq;

		if (lora_session_next_tx_seq(&seq) == 0) {
			lora_hdr_seq_set(&hdr, seq);
			if (lora_packet_encode(&hdr, &ack, sizeof(ack), s ? s->session_key : NULL,
					       tx_buf, sizeof(tx_buf), &tx_len) == 0) {
				(void)lora_radio_send(tx_buf, tx_len);
			}
		}
	}
	return 0;
}

/* --------------------------------------------------------------------------
 * lora_fota_chan subscriber — handles FOTA_START / FOTA_CANCEL
 * -------------------------------------------------------------------------- */

/**
 * @brief Derive LoRa node_id from a sensor UID.
 *
 * Current encoding: UID = (0x0200 << 16) | (node_id << 4) | type
 * This is a temporary mapping — the future sensor_registry API will be
 * the single source of truth for UID→node resolution.
 */
static uint16_t fota_node_id_from_uid(uint32_t uid)
{
	return (uint16_t)((uid >> 4) & 0xFF);
}

static void fota_chan_handler(const struct zbus_channel *chan)
{
	const struct lora_fota_event *evt;

	evt = zbus_chan_const_msg(chan);
	if (!evt) {
		return;
	}

	uint16_t node_id = fota_node_id_from_uid(evt->target_uid);

	switch (evt->action) {
	case LORA_FOTA_START:
		fota_start_handler(node_id, evt->image_size, evt->fota_mode);
		break;
	case LORA_FOTA_CANCEL:
		fota_cancel_handler(node_id);
		break;
	}
}

ZBUS_LISTENER_DEFINE(lora_fota_listener, fota_chan_handler);

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

static int lora_fota_init(void)
{
	fota_sender.state = FOTA_STATE_IDLE;
	fota_rx_active = false;
	k_work_init_delayable(&fota_sender.work, fota_advance_window);
	memset(&fota_sender.src_stream, 0, sizeof(fota_sender.src_stream));
	memset(&fota_rx_stream, 0, sizeof(fota_rx_stream));
	return 0;
}

SYS_INIT(lora_fota_init, APPLICATION, 85);
#endif /* CONFIG_IO_STREAM */
