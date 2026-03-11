/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file http_dashboard.h
 * @brief Public header for the HTTP weather dashboard library.
 *
 * The library self-initialises via SYS_INIT at APPLICATION priority 97.
 * No explicit initialisation call is required by the application.
 *
 * Endpoints served:
 *   GET  /            Live Chart.js dashboard (polls /api/data)
 *   GET  /config      Configuration form page
 *   GET  /api/data    JSON: sensor history ring-buffer contents
 *   GET  /api/config  JSON: current runtime configuration
 *   POST /api/config  Form-encoded: update trigger_interval_ms, sntp_server,
 *                     or trigger action=sntp_resync
 */

#ifndef HTTP_DASHBOARD_HTTP_DASHBOARD_H_
#define HTTP_DASHBOARD_HTTP_DASHBOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

/* No public API — library is fully self-contained via SYS_INIT. */

#ifdef __cplusplus
}
#endif

#endif /* HTTP_DASHBOARD_HTTP_DASHBOARD_H_ */
