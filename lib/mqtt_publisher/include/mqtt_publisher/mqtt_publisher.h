/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file mqtt_publisher.h
 * @brief MQTT sensor data publisher.
 *
 * No public API — enable with CONFIG_MQTT_PUBLISHER=y and the library
 * self-registers via SYS_INIT.
 *
 * Publishes every env_sensor_data event received on sensor_event_chan to
 * an MQTT broker under the topic:
 *   {gateway_name}/{location}/{display_name}/{sensor_type}
 *
 * Payload format:
 *   {"time":<epoch_s>,"value":<float>,"unit":"<unit>"}
 *
 * Broker connection settings (host, port, username, password, gateway name)
 * are stored in the settings subsystem under the "mqttp/" subtree.
 */

#ifndef MQTT_PUBLISHER_MQTT_PUBLISHER_H_
#define MQTT_PUBLISHER_MQTT_PUBLISHER_H_

#if defined(CONFIG_ZTEST)
/**
 * @brief Return the number of events currently in the publish queue.
 *
 * Only available when CONFIG_ZTEST=y.  Used by the mqtt_publisher test suite
 * to verify that events are enqueued without requiring a live broker.
 */
int mqtt_publisher_queue_used(void);
#endif /* CONFIG_ZTEST */

#endif /* MQTT_PUBLISHER_MQTT_PUBLISHER_H_ */
