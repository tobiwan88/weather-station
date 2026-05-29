# ADR-017 — Generic I/O Stream Abstraction

| Field | Value |
|-------|-------|
| **Status** | Proposed |
| **Date** | 2026-05-28 |
| **Deciders** | Project founder |

---

## Context

Firmware-over-the-air (FOTA) code directly calls `flash_area_read()`, `flash_img_buffered_write()`, and `flash_img_init()` with hardcoded partition IDs (`PM_MCUBOOT_SECONDARY`, `slot1_partition`). This creates three problems:

1. **No native_sim testing** — FOTA sender/receiver cannot run without real flash partitions, blocking automated CI for the entire FOTA protocol.
2. **Single-source coupling** — firmware images are always read from the MCUboot secondary slot, preventing sensor-specific firmware images stored in RAM or alternative locations.
3. **No reusable abstraction** — every module that needs to read/write sequential data (firmware images, log files, config blobs) must reimplement its own buffering, offset tracking, and error handling.

The project needs a generic, pluggable I/O stream interface that any module can use, with backends for flash, RAM, and future file/network transports.

---

## Decision

Introduce a generic `io_stream` vtable interface in `lib/io_stream/` that provides Unix-style sequential I/O operations (read, write, seek, tell, size, flush, close) with pluggable backends. The vtable uses a struct of function pointers with per-stream position state, following the same pattern as `remote_transport`. Initial backends are flash (wrapping Zephyr's `flash_img` for progressive erase) and RAM buffer (for native_sim testing). FOTA is refactored to use `io_stream` instead of direct flash access.

---

## Consequences

**Easier:**
- FOTA is fully testable on native_sim using the RAM buffer backend.
- Any module can read/write sequential data through a uniform interface.
- New backends (file, network, encrypted) can be added without changing consumers.
- FOTA sender can read from any image source (flash partition, RAM buffer, HTTP download).

**Harder:**
- One additional abstraction layer between FOTA code and flash operations.
- Flash backend must reconcile `flash_img`'s sequential-write model with the stream's seek capability (writes at non-current position return `-ENOTSUP`).

**Constrained:**
- All stream instances are statically allocated (no heap, per ADR-003).
- Stream backends are Kconfig-gated — each adds code size.
- The vtable pattern adds one indirect call per operation (negligible on MCU, measurable on tight LoRa FOTA timing loops).

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Keep direct `flash_area_*` calls, add native_sim mocks per use case | Duplicates mocking logic across FOTA sender, receiver, and HTTP upload. No reusable abstraction. |
| Use Zephyr's internal `stream_flash` (from `subsys/dfu/stream_flash`) | Internal to Zephyr's img_mgmt, not a public API, no seek/tell, no RAM backend. Not designed for general-purpose streaming. |
| C++-style iostreams with operator overloading | Project is C-only. Zephyr RTOS does not use C++ standard library. |
| Stateless read/write with explicit offset in every call | Loses Unix compatibility, makes sequential streaming verbose, doesn't support `tell()` or `flush()` semantics naturally. |

---

## See also

- Current implementation: `lib/io_stream/` (vtable, flash backend, buffer backend)
- FOTA integration: `lib/lora_radio/src/lora_handler_fota.c`, `lib/http_dashboard/src/fota.c`
- Related ADRs: [ADR-014](ADR-014-mcuboot-fota.md) (FOTA architecture), [ADR-008](ADR-008-kconfig-app-composition.md) (Kconfig composition), [ADR-009](ADR-009-native-sim-first.md) (native_sim testing), [ADR-003](ADR-003-sensor-event-data-model.md) (no heap)
