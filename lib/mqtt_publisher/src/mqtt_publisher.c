/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher.c
 * @brief Publishes sensor events to an MQTT broker.
 *
 * Subscribes to sensor_event_chan via a zbus listener and forwards every
 * env_sensor_data event to the configured MQTT broker under the topic:
 *
 *   {gateway_name}/{location}/{display_name}/{sensor_type}
 *
 * Payload: {"time":<epoch_s>,"value":<float>,"unit":"<unit>"}
 *
 * A dedicated thread owns the MQTT socket.  The zbus callback enqueues events
 * into a K_MSGQ (dropped silently when full); the thread drains that queue and
 * calls mqtt_input() / mqtt_live() in a poll loop.  On disconnect the thread
 * waits CONFIG_MQTT_PUBLISHER_RECONNECT_MS before retrying.
 *
 * Broker settings (host, port, username, password, gateway name) are
 * persisted in the settings subsystem under the "mqttp/" subtree.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>
#include <sensor_registry/sensor_registry.h>
#include <sntp_sync/sntp_sync.h>

#include "mqtt_publisher_format.h"

LOG_MODULE_REGISTER(mqtt_publisher, LOG_LEVEL_INF);

/* --------------------------------------------------------------------------
 * Message queue: zbus callback → MQTT thread
 * -------------------------------------------------------------------------- */
K_MSGQ_DEFINE(s_mqtt_queue, sizeof(struct env_sensor_data), CONFIG_MQTT_PUBLISHER_QUEUE_DEPTH, 4);

/* --------------------------------------------------------------------------
 * MQTT client state
 * -------------------------------------------------------------------------- */
static uint8_t s_rx_buf[512];
static uint8_t s_tx_buf[512];
static struct mqtt_client s_client;
static struct sockaddr_storage s_broker_addr;
static struct zsock_pollfd s_fds[1];
static bool s_connected;

/* --------------------------------------------------------------------------
 * Runtime configuration (seeded from Kconfig, overwritten by settings load)
 * -------------------------------------------------------------------------- */
static char s_broker_host[64] = CONFIG_MQTT_PUBLISHER_BROKER_HOST;
static uint16_t s_broker_port = CONFIG_MQTT_PUBLISHER_BROKER_PORT;
static char s_username[32] = CONFIG_MQTT_PUBLISHER_BROKER_USER;
static char s_password[64] = CONFIG_MQTT_PUBLISHER_BROKER_PASS;
static char s_gateway_name[32] = CONFIG_MQTT_PUBLISHER_GATEWAY_NAME;

/* mqtt_utf8 structs for user_name / password fields (must outlive connect) */
static struct mqtt_utf8 s_user_utf8;
static struct mqtt_utf8 s_pass_utf8;

/* --------------------------------------------------------------------------
 * Settings handler
 * -------------------------------------------------------------------------- */
static int mqtt_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);
	ssize_t ret;

	if (strcmp(key, "server") == 0) {
		ret = read_cb(cb_arg, s_broker_host, sizeof(s_broker_host) - 1);
		if (ret > 0) {
			s_broker_host[ret] = '\0';
		}
	} else if (strcmp(key, "port") == 0) {
		uint16_t broker_port;

		ret = read_cb(cb_arg, &broker_port, sizeof(broker_port));
		if (ret == sizeof(s_broker_port)) {
			s_broker_port = broker_port;
		}
	} else if (strcmp(key, "user") == 0) {
		ret = read_cb(cb_arg, s_username, sizeof(s_username) - 1);
		if (ret > 0) {
			s_username[ret] = '\0';
		}
	} else if (strcmp(key, "pass") == 0) {
		ret = read_cb(cb_arg, s_password, sizeof(s_password) - 1);
		if (ret > 0) {
			s_password[ret] = '\0';
		}
	} else if (strcmp(key, "gw") == 0) {
		ret = read_cb(cb_arg, s_gateway_name, sizeof(s_gateway_name) - 1);
		if (ret > 0) {
			s_gateway_name[ret] = '\0';
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(mqttp, "mqttp", NULL, mqtt_settings_set, NULL, NULL);

/* --------------------------------------------------------------------------
 * MQTT event handler
 * -------------------------------------------------------------------------- */
static void mqtt_evt_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			s_connected = true;
			LOG_INF("connected to %s:%u", s_broker_host, s_broker_port);
		} else {
			LOG_ERR("CONNACK error %d", evt->result);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("disconnected (reason %d)", evt->result);
		s_connected = false;
		break;

	case MQTT_EVT_PUBACK:
	case MQTT_EVT_PUBCOMP:
		/* QoS 1/2 acknowledgements — not used (QoS 0 publish only) */
		break;

	default:
		break;
	}
}

/* --------------------------------------------------------------------------
 * Publish one event
 * -------------------------------------------------------------------------- */
static void publish_event(const struct env_sensor_data *evt)
{
	char topic_buf[128];
	char payload_buf[128];

	const char *location = sensor_registry_get_location(evt->sensor_uid);
	const char *name = sensor_registry_get_display_name(evt->sensor_uid);
	int64_t epoch_s = sntp_sync_get_epoch_ms() / 1000;

	mqtt_publisher_build_topic(s_gateway_name, location, name, evt->type, topic_buf,
				   sizeof(topic_buf));

	int payload_len = mqtt_publisher_build_payload(epoch_s, evt->type, evt->q31_value,
						       payload_buf, sizeof(payload_buf));

	if (payload_len <= 0 || payload_len >= (int)sizeof(payload_buf)) {
		LOG_WRN("payload build failed or truncated (%d)", payload_len);
		return;
	}

	struct mqtt_publish_param param = {0};

	param.message.topic.topic.utf8 = (uint8_t *)topic_buf;
	param.message.topic.topic.size = strlen(topic_buf);
	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	param.message.payload.data = (uint8_t *)payload_buf;
	param.message.payload.len = (uint32_t)payload_len;
	param.message_id = sys_rand16_get();

	int rc = mqtt_publish(&s_client, &param);

	if (rc != 0) {
		LOG_WRN("mqtt_publish failed: %d — marking disconnected", rc);
		s_connected = false;
	}
}

/* --------------------------------------------------------------------------
 * Broker connect
 * -------------------------------------------------------------------------- */
static int broker_resolve(void)
{
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res;
	char port_str[8];

	snprintf(port_str, sizeof(port_str), "%u", s_broker_port);

	int rc = zsock_getaddrinfo(s_broker_host, port_str, &hints, &res);

	if (rc != 0) {
		LOG_ERR("DNS lookup for '%s' failed: %d", s_broker_host, rc);
		return -ENOENT;
	}

	memcpy(&s_broker_addr, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);
	return 0;
}

static int broker_connect(void)
{
	if (broker_resolve() != 0) {
		return -ENOENT;
	}

	mqtt_client_init(&s_client);

	s_client.broker = &s_broker_addr;
	s_client.evt_cb = mqtt_evt_handler;
	s_client.client_id.utf8 = (uint8_t *)s_gateway_name;
	s_client.client_id.size = strlen(s_gateway_name);
	s_client.protocol_version = MQTT_VERSION_3_1_1;
	s_client.rx_buf = s_rx_buf;
	s_client.rx_buf_size = sizeof(s_rx_buf);
	s_client.tx_buf = s_tx_buf;
	s_client.tx_buf_size = sizeof(s_tx_buf);
	s_client.transport.type = MQTT_TRANSPORT_NON_SECURE;
	s_client.keepalive = CONFIG_MQTT_PUBLISHER_KEEPALIVE;

	if (s_username[0] != '\0') {
		s_user_utf8.utf8 = (uint8_t *)s_username;
		s_user_utf8.size = strlen(s_username);
		s_client.user_name = &s_user_utf8;

		s_pass_utf8.utf8 = (uint8_t *)s_password;
		s_pass_utf8.size = strlen(s_password);
		s_client.password = &s_pass_utf8;
	}

	s_connected = false;

	int rc = mqtt_connect(&s_client);

	if (rc != 0) {
		LOG_ERR("mqtt_connect failed: %d", rc);
		return rc;
	}

	/* Wait up to 5 s for CONNACK */
	s_fds[0].fd = s_client.transport.tcp.sock;
	s_fds[0].events = ZSOCK_POLLIN;

	if (zsock_poll(s_fds, 1, 5000) > 0) {
		mqtt_input(&s_client);
	}

	if (!s_connected) {
		LOG_ERR("no CONNACK from %s:%u", s_broker_host, s_broker_port);
		mqtt_abort(&s_client);
		return -ETIMEDOUT;
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * MQTT thread
 * -------------------------------------------------------------------------- */
static void mqtt_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		LOG_INF("connecting to %s:%u ...", s_broker_host, s_broker_port);

		if (broker_connect() != 0) {
			k_sleep(K_MSEC(CONFIG_MQTT_PUBLISHER_RECONNECT_MS));
			continue;
		}

		while (s_connected) {
			/* Poll for incoming data (100 ms timeout for keepalive) */
			s_fds[0].fd = s_client.transport.tcp.sock;
			s_fds[0].events = ZSOCK_POLLIN;

			int ret = zsock_poll(s_fds, 1, 100);

			if (ret > 0 && (s_fds[0].revents & ZSOCK_POLLIN)) {
				int rc = mqtt_input(&s_client);

				if (rc != 0) {
					LOG_WRN("mqtt_input: %d", rc);
					s_connected = false;
					break;
				}
			}

			/* Send keepalive ping if needed */
			int rc = mqtt_live(&s_client);

			if (rc != 0 && rc != -EAGAIN) {
				LOG_WRN("mqtt_live: %d", rc);
				s_connected = false;
				break;
			}

			/* Drain event queue */
			struct env_sensor_data evt;

			while (s_connected && k_msgq_get(&s_mqtt_queue, &evt, K_NO_WAIT) == 0) {
				publish_event(&evt);
			}
		}

		mqtt_disconnect(&s_client, NULL);
		k_sleep(K_MSEC(CONFIG_MQTT_PUBLISHER_RECONNECT_MS));
	}
}

K_THREAD_STACK_DEFINE(s_mqtt_stack, CONFIG_MQTT_PUBLISHER_STACK_SIZE);
static struct k_thread s_mqtt_thread;

/* --------------------------------------------------------------------------
 * zbus listener — enqueue only, non-blocking
 * -------------------------------------------------------------------------- */
static void mqtt_sensor_event_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);

	/* Drop silently if the queue is full (thread is busy reconnecting) */
	k_msgq_put(&s_mqtt_queue, evt, K_NO_WAIT);
}

ZBUS_LISTENER_DEFINE(mqtt_publisher_listener, mqtt_sensor_event_cb);

/* --------------------------------------------------------------------------
 * Initialization
 * -------------------------------------------------------------------------- */
static int mqtt_publisher_init(void)
{
	int rc = settings_load_subtree("mqttp");

	if (rc != 0) {
		LOG_WRN("settings_load_subtree(mqttp) failed: %d", rc);
	}

	rc = zbus_chan_add_obs(&sensor_event_chan, &mqtt_publisher_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("failed to subscribe to sensor_event_chan: %d", rc);
		return rc;
	}

	k_thread_create(&s_mqtt_thread, s_mqtt_stack, K_THREAD_STACK_SIZEOF(s_mqtt_stack),
			mqtt_thread_fn, NULL, NULL, NULL, CONFIG_MQTT_PUBLISHER_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&s_mqtt_thread, "mqtt_pub");

	return 0;
}

SYS_INIT(mqtt_publisher_init, APPLICATION, 98);

/* --------------------------------------------------------------------------
 * Optional shell commands
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_MQTT_PUBLISHER_SHELL)

#	include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "gateway : %s", s_gateway_name);
	shell_print(sh, "broker  : %s:%u", s_broker_host, s_broker_port);
	shell_print(sh, "user    : %s", s_username[0] ? s_username : "(none)");
	shell_print(sh, "state   : %s", s_connected ? "connected" : "disconnected");
	return 0;
}

static int cmd_set_server(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "usage: mqtt_pub set server <host> <port>");
		return -EINVAL;
	}

	strncpy(s_broker_host, argv[1], sizeof(s_broker_host) - 1);
	s_broker_host[sizeof(s_broker_host) - 1] = '\0';

	long port = strtol(argv[2], NULL, 10);

	if (port <= 0 || port > 65535) {
		shell_error(sh, "invalid port");
		return -EINVAL;
	}
	s_broker_port = (uint16_t)port;

	settings_save_one("mqttp/server", s_broker_host, strlen(s_broker_host) + 1);
	settings_save_one("mqttp/port", &s_broker_port, sizeof(s_broker_port));

	shell_print(sh, "broker set to %s:%u — reconnect will pick up the change", s_broker_host,
		    s_broker_port);
	return 0;
}

static int cmd_set_auth(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "usage: mqtt_pub set auth <user> <pass>");
		return -EINVAL;
	}

	strncpy(s_username, argv[1], sizeof(s_username) - 1);
	s_username[sizeof(s_username) - 1] = '\0';
	strncpy(s_password, argv[2], sizeof(s_password) - 1);
	s_password[sizeof(s_password) - 1] = '\0';

	settings_save_one("mqttp/user", s_username, strlen(s_username) + 1);
	settings_save_one("mqttp/pass", s_password, strlen(s_password) + 1);

	shell_print(sh, "credentials saved — reconnect will pick up the change");
	return 0;
}

static int cmd_set_gateway(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "usage: mqtt_pub set gateway <name>");
		return -EINVAL;
	}

	strncpy(s_gateway_name, argv[1], sizeof(s_gateway_name) - 1);
	s_gateway_name[sizeof(s_gateway_name) - 1] = '\0';

	settings_save_one("mqttp/gw", s_gateway_name, strlen(s_gateway_name) + 1);

	shell_print(sh, "gateway name set to '%s'", s_gateway_name);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_set, SHELL_CMD_ARG(server, NULL, "Set broker <host> <port>", cmd_set_server, 3, 0),
	SHELL_CMD_ARG(auth, NULL, "Set <user> <pass>", cmd_set_auth, 3, 0),
	SHELL_CMD_ARG(gateway, NULL, "Set gateway <name>", cmd_set_gateway, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_mqtt_pub, SHELL_CMD(status, NULL, "Show current MQTT publisher status", cmd_status),
	SHELL_CMD(set, &sub_set, "Change a setting", NULL), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(mqtt_pub, &sub_mqtt_pub, "MQTT publisher commands", NULL);

#endif /* CONFIG_MQTT_PUBLISHER_SHELL */

#if defined(CONFIG_ZTEST)
#	include <mqtt_publisher/mqtt_publisher.h>

int mqtt_publisher_queue_used(void)
{
	return k_msgq_num_used_get(&s_mqtt_queue);
}
#endif /* CONFIG_ZTEST */
