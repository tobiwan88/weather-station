/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HTTP_DASHBOARD_PROCESS_POST_H_
#define HTTP_DASHBOARD_PROCESS_POST_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Parse a form-encoded POST body and dispatch configuration commands.
 *
 * Recognised fields:
 *   - trigger_interval_ms=<ms>  — publishes CONFIG_CMD_SET_TRIGGER_INTERVAL
 *   - sntp_server=<host>        — updates the cached SNTP server string
 *   - action=sntp_resync        — publishes CONFIG_CMD_SNTP_RESYNC
 *   - action=add_location&loc_name=<n>    — calls location_registry_add()
 *   - action=remove_location&loc_name=<n> — calls location_registry_remove()
 *   - sensor_<uid>_<field>=<v>  — updates sensor metadata (requires
 *                                  CONFIG_SENSOR_REGISTRY_USER_META)
 *
 * @param body Raw accumulated POST body bytes (not NUL-terminated required,
 *             but must fit in 1024 bytes; excess is silently truncated).
 * @param len  Number of bytes in @p body.
 */
void process_post(const uint8_t *body, size_t len);

/**
 * @brief Return the most recently set trigger interval in milliseconds.
 *
 * Returns 0 if no trigger interval has been set via POST.
 */
uint32_t config_state_get_trigger_ms(void);

/**
 * @brief Copy the most recently set SNTP server hostname into @p out.
 *
 * Thread-safe: the copy is performed under a spinlock. @p out receives an
 * empty string if no SNTP server has been set via POST.
 *
 * @param out  Destination buffer.
 * @param len  Size of @p out in bytes (must be > 0).
 */
void config_state_copy_sntp_server(char *out, size_t len);

#endif /* HTTP_DASHBOARD_PROCESS_POST_H_ */
