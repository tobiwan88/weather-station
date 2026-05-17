/* SPDX-License-Identifier: Apache-2.0 */

#define LOG_MODULE_NAME sensor_node_tx
#define LOG_LEVEL       CONFIG_SENSOR_NODE_TX_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <math.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <lora_node/lora_node.h>

/* Q31 encoding helpers */

static int32_t temperature_c_to_q31(double temp_c)
{
	double normalized = (temp_c + 40.0) / 125.0;
	if (normalized < -1.0) {
		normalized = -1.0;
	}
	if (normalized > 1.0) {
		normalized = 1.0;
	}
	return (int32_t)(normalized * (double)INT32_MAX);
}

static int32_t pct_to_q31(double pct)
{
	double normalized = pct / 100.0;
	if (normalized < 0.0) {
		normalized = 0.0;
	}
	if (normalized > 1.0) {
		normalized = 1.0;
	}
	return (int32_t)(normalized * (double)INT32_MAX);
}

static int32_t pressure_pa_to_q31(double pa)
{
	double normalized = (pa - 30000.0) / 80000.0;
	if (normalized < 0.0) {
		normalized = 0.0;
	}
	if (normalized > 1.0) {
		normalized = 1.0;
	}
	return (int32_t)(normalized * (double)INT32_MAX);
}

static int32_t gas_res_to_q31(double ohms)
{
	double log_val = log10(ohms);
	double normalized = (log_val - 1.0) / 5.0;
	if (normalized < 0.0) {
		normalized = 0.0;
	}
	if (normalized > 1.0) {
		normalized = 1.0;
	}
	return (int32_t)(normalized * (double)INT32_MAX);
}

/* Wire format: 1B sensor_type + 4B q31_value */
#define READING_TYPE_TEMPERATURE 0x01
#define READING_TYPE_HUMIDITY    0x02
#define READING_TYPE_PRESSURE    0x03
#define READING_TYPE_GAS_RES     0x04

struct sensor_reading {
	uint8_t type;
	int32_t q31_value;
} __packed;

BUILD_ASSERT(sizeof(struct sensor_reading) == 5, "reading must be 5 bytes");

/* Sensor device handles */
static const struct device *bme_dev;

/* Periodic work item */
static struct k_work_delayable tx_work;

static void tx_work_fn(struct k_work *work)
{
	(void)work;

	if (!bme_dev || !device_is_ready(bme_dev)) {
		LOG_WRN("BME688 not ready");
		goto reschedule;
	}

	struct sensor_value temp, hum, press, gas;
	struct sensor_reading readings[CONFIG_SENSOR_NODE_TX_MAX_READINGS];
	size_t count = 0;

	/* Trigger a sample */
	if (sensor_sample_fetch(bme_dev) < 0) {
		LOG_WRN("sensor_sample_fetch failed");
		goto reschedule;
	}

	/* Read temperature */
	if (sensor_channel_get(bme_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) == 0) {
		readings[count].type = READING_TYPE_TEMPERATURE;
		readings[count].q31_value = temperature_c_to_q31(sensor_value_to_double(&temp));
		count++;
	}

	/* Read humidity */
	if (sensor_channel_get(bme_dev, SENSOR_CHAN_HUMIDITY, &hum) == 0) {
		readings[count].type = READING_TYPE_HUMIDITY;
		readings[count].q31_value = pct_to_q31(sensor_value_to_double(&hum));
		count++;
	}

	/* Read pressure */
	if (sensor_channel_get(bme_dev, SENSOR_CHAN_PRESS, &press) == 0) {
		readings[count].type = READING_TYPE_PRESSURE;
		readings[count].q31_value = pressure_pa_to_q31(sensor_value_to_double(&press));
		count++;
	}

	/* Read gas resistance */
	if (sensor_channel_get(bme_dev, SENSOR_CHAN_GAS_RES, &gas) == 0) {
		readings[count].type = READING_TYPE_GAS_RES;
		readings[count].q31_value = gas_res_to_q31(sensor_value_to_double(&gas));
		count++;
	}

	if (count == 0) {
		LOG_WRN("no readings available");
		goto reschedule;
	}

	/* Transmit */
	int ret = lora_node_transmit((const uint8_t *)readings, count);
	if (ret < 0) {
		LOG_ERR("lora_node_transmit failed: %d", ret);
	} else {
		LOG_INF("transmitted %zu readings", count);
	}

reschedule:
	k_work_reschedule(&tx_work, K_SECONDS(CONFIG_SENSOR_NODE_TX_INTERVAL_S));
}

static int sensor_node_tx_init(void)
{
	bme_dev = DEVICE_DT_GET_ANY(bosch_bme680);
	if (!device_is_ready(bme_dev)) {
		LOG_WRN("BME688 not found on I2C");
		return -ENODEV;
	}

	LOG_INF("BME688 found");

	/* Initialize LoRa TX */
	int ret = lora_node_init();
	if (ret < 0) {
		LOG_ERR("lora_node_init failed: %d", ret);
		return ret;
	}

	/* Schedule first transmission after a short delay */
	k_work_init_delayable(&tx_work, tx_work_fn);
	k_work_reschedule(&tx_work, K_SECONDS(2));

	return 0;
}

SYS_INIT(sensor_node_tx_init, APPLICATION, 90);
