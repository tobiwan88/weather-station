/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sensor_trigger.h
 * @brief Sensor trigger channel for the weather-station.
 *
 * Any publisher (periodic timer, button ISR, MQTT command handler) may
 * post a sensor_trigger_event on sensor_trigger_chan. Each sensor driver
 * independently subscribes to this channel and publishes an
 * env_sensor_data event in response.
 *
 * Setting target_uid = 0 is a broadcast — all registered sensors will
 * sample and publish. A non-zero target_uid addresses one specific sensor.
 */

#ifndef SENSOR_TRIGGER_SENSOR_TRIGGER_H_
#define SENSOR_TRIGGER_SENSOR_TRIGGER_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Origin of the trigger event. */
enum trigger_source {
	TRIGGER_SOURCE_TIMER,   /**< Periodic sampling timer      */
	TRIGGER_SOURCE_BUTTON,  /**< Physical button press        */
	TRIGGER_SOURCE_MQTT,    /**< Remote MQTT command          */
	TRIGGER_SOURCE_STARTUP, /**< One-shot trigger at boot     */
};

/**
 * @brief Trigger event transmitted on sensor_trigger_chan.
 */
struct sensor_trigger_event {
	enum trigger_source source; /**< Who originated the trigger */
	uint32_t target_uid;        /**< 0 = broadcast all sensors  */
};

/** zbus channel carrying sensor_trigger_event (defined in sensor_trigger.c). */
ZBUS_CHAN_DECLARE(sensor_trigger_chan);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_TRIGGER_SENSOR_TRIGGER_H_ */
