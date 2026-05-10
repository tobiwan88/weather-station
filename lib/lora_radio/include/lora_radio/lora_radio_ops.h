/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LORA_RADIO_LORA_RADIO_OPS_H_
#define LORA_RADIO_LORA_RADIO_OPS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lora_radio_ops {
	int (*init)(void);
	int (*set_frequency)(uint32_t freq_hz);
	int (*set_modem_config)(uint8_t sf, uint32_t bw);
	int (*set_tx_power)(int8_t dbm);
	int (*tx)(const uint8_t *data, uint8_t len);
	int (*rx)(uint8_t *buf, uint8_t max_len, int32_t timeout_ms);
	int (*rx_enable)(bool enable);
	int16_t (*rssi)(void);
	int8_t (*snr)(void);
};

extern const struct lora_radio_ops *lora_radio_ops;

#ifdef __cplusplus
}
#endif

#endif /* LORA_RADIO_LORA_RADIO_OPS_H_ */
