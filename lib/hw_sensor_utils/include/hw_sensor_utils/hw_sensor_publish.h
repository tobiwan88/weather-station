/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HW_SENSOR_UTILS_HW_SENSOR_PUBLISH_H_
#define HW_SENSOR_UTILS_HW_SENSOR_PUBLISH_H_

#include <sensor_event/sensor_event.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int hw_sensor_publish(uint32_t sensor_uid, enum sensor_type type, int32_t q31_value);

#ifdef __cplusplus
}
#endif

#endif /* HW_SENSOR_UTILS_HW_SENSOR_PUBLISH_H_ */
