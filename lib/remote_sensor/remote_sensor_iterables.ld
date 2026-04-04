/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linker section for remote_transport iterable structs.
 * Defines _remote_transport_list_start/_end used by STRUCT_SECTION_FOREACH.
 *
 * ROM section: vtables are const (function pointers + const strings).
 */

ITERABLE_SECTION_ROM(remote_transport, 4)
