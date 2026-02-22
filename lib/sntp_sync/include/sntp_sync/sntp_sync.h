/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Returns true if at least one successful NTP sync has completed.
 */
bool sntp_sync_is_ready(void);

/**
 * @brief Returns the current wall-clock time in milliseconds since the Unix epoch.
 *
 * If no sync has been performed yet, returns a value derived from k_uptime_get()
 * (i.e. milliseconds since boot, not since epoch).
 */
int64_t sntp_sync_get_epoch_ms(void);
