/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Linker section for fake_sensor_entry iterable structs.
 * Defines _fake_sensor_entry_list_start/end used by STRUCT_SECTION_FOREACH.
 */

ITERABLE_SECTION_RAM(fake_sensor_entry, 4)
