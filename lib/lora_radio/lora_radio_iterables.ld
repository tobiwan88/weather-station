/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linker section for lora_transport_info iterable structs.
 * Defines _lora_transport_info_list_start/_end used by STRUCT_SECTION_FOREACH.
 *
 * ROM section: structs are const (name + proto + caps, no function pointers).
 */

ITERABLE_SECTION_ROM(lora_transport_info, 4)
