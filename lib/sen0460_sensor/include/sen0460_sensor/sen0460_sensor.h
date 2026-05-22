/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEN0460_SENSOR_SEN0460_SENSOR_H_
#define SEN0460_SENSOR_SEN0460_SENSOR_H_

#include <stdbool.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SEN0460 private sensor attributes (power management) */
enum sen0460_attribute {
	SEN0460_ATTR_SUSPEND = SENSOR_ATTR_PRIV_START,
	SEN0460_ATTR_RESUME,
};

#ifdef CONFIG_SENSOR_ASYNC_API
int sen0460_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder);
#endif

int sen0460_sensor_enable(void);
int sen0460_sensor_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* SEN0460_SENSOR_SEN0460_SENSOR_H_ */
