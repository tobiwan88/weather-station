/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linker section for HTTP resource descriptors registered with dashboard_svc.
 * Defines _http_resource_desc_dashboard_svc_list_start/end used by
 * HTTP_SERVICE_DEFINE / HTTP_RESOURCE_DEFINE.
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(http_resource_desc_dashboard_svc, Z_LINK_ITERABLE_SUBALIGN)
