# REQ-IO-STREAM — Generic I/O Stream Abstraction

## Status
draft

## Version
0.1

## Context
FOTA code directly calls `flash_area_read()`, `flash_img_buffered_write()`, and `flash_img_init()` with hardcoded partition IDs. This blocks native_sim testing, prevents sensor-specific firmware images, and provides no reusable abstraction for sequential I/O. A generic stream interface with pluggable backends solves all three problems.

Constrained by ADR-017 (Generic I/O Stream Abstraction).

## Functional Requirements

### REQ-IO-STREAM-001 — Stream Vtable Interface
The system shall provide a `struct io_stream` vtable with function pointers for `read`, `write`, `seek`, `tell`, `size`, `flush`, and `close`. All operations shall return `ssize_t` for data transfer (bytes read/written or negative errno) or `int` for control operations.

### REQ-IO-STREAM-002 — Unix-Style Position Tracking
Each stream instance shall maintain an internal position cursor. `read()` and `write()` shall advance the cursor by the number of bytes transferred. `seek()` shall reposition the cursor relative to beginning (`IO_STREAM_SEEK_SET`), current position (`IO_STREAM_SEEK_CUR`), or end (`IO_STREAM_SEEK_END`). `tell()` shall return the current cursor position.

### REQ-IO-STREAM-003 — Flash Stream Backend
The system shall provide a flash stream backend (`io_stream_flash`) that wraps Zephyr's `flash_img` API. Reads shall use `flash_area_read()`. Writes shall use `flash_img_buffered_write()` for progressive erase. The flash stream shall be configurable by partition ID or DTS label at initialization time.

### REQ-IO-STREAM-004 — Buffer Stream Backend
The system shall provide a RAM buffer stream backend (`io_stream_buffer`) backed by a caller-provided buffer. This backend shall support full read/write/seek/tell operations for native_sim testing and small data transfers. Write beyond buffer capacity shall return `-ENOSPC`. Read past valid data shall return 0 (EOF).

### REQ-IO-STREAM-005 — Static Allocation
All stream instances and backend state shall be statically allocated (stack, BSS, or `static`). No `malloc`, `k_malloc`, or heap allocation shall be used, per ADR-003.

### REQ-IO-STREAM-006 — Kconfig-Gated Backends
Each stream backend shall be independently Kconfig-gated. The flash backend shall depend on `FLASH_MAP` and `IMG_MANAGER`. The buffer backend shall have no hardware dependencies and shall be available on all platforms.

### REQ-IO-STREAM-007 — Unsupported Operation Semantics
When a backend cannot support a requested operation (e.g., seeking and writing to flash at a non-current position), it shall return `-ENOTSUP`. Null function pointers in the vtable shall be treated as `-ENOSYS` by wrapper helpers.

### REQ-IO-STREAM-008 — FOTA Sender Integration
The LoRa FOTA sender shall read firmware chunks through an `io_stream` instance instead of calling `flash_area_read()` directly. The stream source shall be selectable (flash partition on hardware, RAM buffer on native_sim).

### REQ-IO-STREAM-009 — FOTA Receiver Integration
The LoRa FOTA receiver shall write received firmware chunks through an `io_stream` instance instead of calling `flash_img_buffered_write()` directly. Sequential writes shall not require explicit seek.

### REQ-IO-STREAM-010 — HTTP FOTA Upload Integration
The HTTP FOTA upload handler shall write uploaded image data through an `io_stream` instance. The final chunk shall trigger `stream->flush()` to commit buffered data.

### REQ-IO-STREAM-011 — Native_sim FOTA Testing
FOTA sender and receiver shall be testable on native_sim using RAM buffer streams. A static test image buffer shall be provided for native_sim FOTA tests.

## Dependencies
- REQ-FOTA-003: HTTP FOTA upload (uses stream for writing)
- REQ-FOTA-008: Sensor node FOTA over LoRa (uses stream for reading/writing)
- REQ-LORA-008: FOTA window protocol (sender reads through stream)

## Constraints
- No heap allocation (ADR-003)
- Kconfig-only composition (ADR-008)
- native_sim first — buffer backend must work without flash hardware (ADR-009)
- Flash backend uses `flash_img` for progressive erase (ADR-014)
- All stream operations are synchronous (no async callbacks)
- Vtable indirect call overhead is acceptable for FOTA timing (ADR-017)

## Acceptance Criteria
- [ ] [native_sim] ztest: buffer stream read/write/seek/tell/size all pass
- [ ] [native_sim] ztest: buffer stream boundary conditions (EOF, ENOSPC, invalid seek)
- [ ] [native_sim] ztest: vtable null dispatch returns -ENOSYS
- [ ] [native_sim] Integration: FOTA sender reads from buffer stream, delivers chunks via LoRa loopback
- [ ] [native_sim] Integration: FOTA receiver writes to buffer stream, ACKs correctly
- [ ] [native_sim] Integration: HTTP upload writes through flash stream (native_sim flash simulator)
- [ ] [hil] Board: FOTA sender reads from flash stream, transfers to sensor node
- [ ] [hil] Board: FOTA receiver writes to flash stream, boots after apply
- [ ] [pre-commit] Code style: all new files pass clang-format, pre-commit hooks
- [ ] [twister] All tests pass on native_sim/native/64

## Related ADRs
- ADR-017: Generic I/O Stream Abstraction
- ADR-014: MCUboot FOTA and Secure Firmware Update
- ADR-008: Kconfig-Only App Composition
- ADR-009: native_sim First
- ADR-003: Sensor Event Data Model (no heap)

## Related Zephyr subsystems
- CONFIG_IMG_MANAGER
- CONFIG_FLASH_MAP
- CONFIG_FLASH
- CONFIG_ZTEST
