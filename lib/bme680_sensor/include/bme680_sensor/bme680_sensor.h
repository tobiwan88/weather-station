/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BME680_SENSOR_BME680_SENSOR_H_
#define BME680_SENSOR_BME680_SENSOR_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int bme680_sensor_enable(void);
int bme680_sensor_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* BME680_SENSOR_BME680_SENSOR_H_ */
