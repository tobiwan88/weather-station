/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher.h
 * @brief MQTT sensor data publisher.
 *
 * Publishes every env_sensor_data event received on sensor_event_chan to
 * an MQTT broker under the topic:
 *   {gateway_name}/{location}/{display_name}/{sensor_type}
 *
 * Payload format:
 *   {"time":<epoch_s>,"value":<float>,"unit":"<unit>"}
 *
 * Broker connection settings (host, port, username, password, gateway name)
 * are stored in the settings subsystem under the "config/mqtt/" subtree.
 */

#ifndef MQTT_PUBLISHER_MQTT_PUBLISHER_H_
#define MQTT_PUBLISHER_MQTT_PUBLISHER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of the current MQTT publisher configuration. */
struct mqtt_publisher_config {
	bool enabled;
	char host[64];
	uint16_t port;
	char username[32];
	char gateway_name[32];
	uint16_t keepalive;
};

/**
 * @brief Enable or disable the MQTT publisher.
 *
 * @param enabled true to enable and reconnect, false to stop and disconnect.
 * @return 0 on success, negative errno on failure.
 */
int mqtt_publisher_set_enabled(bool enabled);

/**
 * @brief Update broker connection parameters.
 *
 * @param broker Pointer to the new broker configuration.
 * @return 0 on success, negative errno on failure.
 */
int mqtt_publisher_set_broker(const struct mqtt_publisher_config *broker);

/**
 * @brief Update authentication credentials.
 *
 * @param username Broker username (may be empty).
 * @param password Broker password (may be empty).
 * @return 0 on success, negative errno on failure.
 */
int mqtt_publisher_set_auth(const char *username, const char *password);

/**
 * @brief Update the gateway name used as MQTT client ID and topic prefix.
 *
 * @param name Gateway name string.
 * @return 0 on success, negative errno on failure.
 */
int mqtt_publisher_set_gateway_name(const char *name);

/**
 * @brief Snapshot the current runtime configuration.
 *
 * @param out Pointer to the struct to populate.
 */
void mqtt_publisher_get_config(struct mqtt_publisher_config *out);

#if defined(CONFIG_ZTEST)
/**
 * @brief Return the number of events currently in the publish queue.
 *
 * Only available when CONFIG_ZTEST=y.  Used by the mqtt_publisher test suite
 * to verify that events are enqueued without requiring a live broker.
 */
int mqtt_publisher_queue_used(void);
#endif /* CONFIG_ZTEST */

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PUBLISHER_MQTT_PUBLISHER_H_ */
