# SEN0460 Functional Driver Design

**Date:** 2026-05-22
**Status:** Draft
**Author:** AI Assistant

## Problem

The SEN0460 PM2.5 sensor library (`lib/sen0460_sensor`) is a stub that proves the
hardware sensor architecture but returns `-ENOSYS` on read. The outdoor sensor node
needs functional PM1.0/PM2.5/PM10 readings from this sensor over I2C, integrated
into the trigger-driven pipeline, with low-power management for battery operation.

## Goals

1. Implement a Zephyr sensor driver using the **read/decode API** (`sensor_submit_t` +
   `sensor_decoder_api`) — the new upstreamable Zephyr sensor model
2. Wire the driver into the weather-station zbus wrapper (trigger → read + decode → publish)
3. PM1.0, PM2.5, PM10 standard concentration readings (µg/m³) only — health-relevant subset
4. Aggressive power management: wake → 10s settle → read → sleep, triggered every 5 minutes
5. Follow the BME680 pattern: driver layer (upstreamable) + wrapper layer (weather-station integration)
6. No change to `main.c` — self-wired via `SYS_INIT`

## Non-Goals

- Particle count registers (0x11–0x1B) — deferred
- Atmospheric concentration variants (0x0B–0x0F) — deferred
- Runtime override of settle delay or sample interval via `config_cmd_chan` — deferred
- Upstreaming the driver to Zephyr proper — this is the "first living in our library" step
- Changing existing `sensor_event_chan` or `env_sensor_data` structure

## SEN0460 I2C Protocol

**I2C address:** `0x19`

| Register | R/W | Size | Content |
|---|---|---|---|
| `0x01` | W | 1B | Power control: `0x01` = low-power, `0x02` = awake |
| `0x05` | R | 2B | PM1.0 standard concentration (µg/m³, big-endian `uint16_t`) |
| `0x07` | R | 2B | PM2.5 standard concentration (µg/m³, big-endian `uint16_t`) |
| `0x09` | R | 2B | PM10 standard concentration (µg/m³, big-endian `uint16_t`) |
| `0x1D` | R | 1B | Firmware version |

Registers `0x05`–`0x0A` form a contiguous 6-byte block (three 2-byte `uint16_t` values)
that can be read in a single I2C burst.

**Key characteristics:**
- Sensor self-samples continuously (internal fan + laser run autonomously at ~1Hz)
- No "start conversion" register — registers always return the latest cached reading
- `begin()` detection via I2C ACK on address
- Device detection via `i2c_is_ready_dt()` at init
- Comprehensive response time ≤10s — fan + laser need up to 10s to stabilize after wake
- Active current ~100mA, standby ≤2mA

## Architecture

### Library Layout

```
lib/sen0460_sensor/
├── CMakeLists.txt              # Updated: adds I2C + RTIO deps
├── Kconfig                      # Updated: settle time, sample interval, RTIO
├── include/sen0460_sensor/
│   └── sen0460_sensor.h        # Public API: enable/disable (unchanged)
└── src/
    ├── sen0460_driver.c        # NEW: Zephyr sensor driver (read/decode API)
    └── sen0460_sensor.c        # MODIFIED: zbus wrapper → functional reads
```

### Layer Split (BME680 Pattern)

**Layer 1 — `sen0460_driver.c` (upstreamable):**
- Implements `sensor_submit_t` — non-blocking RTIO submit for I2C reads into caller-provided buffer
- Implements `sensor_decoder_api` — stateless decode functions:
  - `.decode`: unpacks 6 raw bytes → 3× `q31_t` values (one per PM channel)
  - `.get_frame_count`: always returns 1 (single-frame sensor, no FIFO)
  - `.has_trigger`: returns false (no trigger info in the data stream)
- Implements `sensor_attr_set_t` for power suspend/resume
- I2C communication via Zephyr I2C API (`i2c_burst_read_dt`, `i2c_burst_write_dt`)
- Registers via `SENSOR_DEVICE_DT_INST_DEFINE`
- **RTIO fallback:** When `CONFIG_RTIO=n`, `sensor_submit_t` uses a `k_work` to perform
  the blocking I2C read and complete the RTIO SQE manually
- No knowledge of zbus, `sensor_type`, or weather-station concepts
- Decoder outputs Zephyr-standard `q31_t` values: 0 → 0 µg/m³, `INT32_MAX` → 1000 µg/m³

**Layer 2 — `sen0460_sensor.c` (weather-station wrapper):**
- Gets the device via `DEVICE_DT_GET_ANY(dfr_sen0460)`
- Subscribes to `sensor_trigger_chan` via `ZBUS_LISTENER_DEFINE`
- Creates a polling I/O device via `SENSOR_DT_READ_IODEV` for the three PM channels
- Allocates an `RTIO_DEFINE` context for the read
- Trigger callback: wakes sensor → starts settle timer → defers to `k_work_delayable`
- Settle work: `sensor_read()` (blocking on iodev) → `decoder->decode()` per channel → populate `env_sensor_data` → `zbus_chan_pub()`
- Manages the settle-delay work item and power state machine
- Registers with `sensor_registry` (label `"sen0460"`, UID `0x0021`)

**Upstream path:** `sen0460_driver.c` moves to `zephyr/drivers/sensor/sen0460/` +
a DT binding in `zephyr/dts/bindings/sensor/dfr,sen0460.yaml`. The wrapper stays in
this repo with only a Kconfig dependency change.

### Data Flow (Read/Decode Pipeline)

```
[Timer trigger, 5min] → sensor_trigger_chan → [sen0460 zbus listener]
    → attr_set(PM_RESUME) → write 0x02 to reg 0x01
    → submit k_work_delayable(CONFIG_SEN0460_SENSOR_SETTLE_MS=10000)

[After settle delay]:
    → sensor_read(sen0460_iodev, &rtio_ctx, buf, 6)   // blocking polling read
    → rtio_cqe_consume_block(&rtio_ctx)                // wait for I2C completion
    → decoder->decode(buf, {PM_1_0, 0}, &fit, 1, &q31_data[0])
    → decoder->decode(buf, {PM_2_5, 0}, &fit, 1, &q31_data[1])
    → decoder->decode(buf, {PM_10, 0},  &fit, 1, &q31_data[2])
    → For each q31_data[i]:
        env_sensor_data = {
            .sensor_uid   = state.uid,
            .type         = chan_to_sensor_type(q31_data[i].header.channel),
            .q31_value    = q31_data[i].q31,   // q31_t is int32_t, same as our field
            .timestamp_ms = k_uptime_get(),
        }
        zbus_chan_pub(&sensor_event_chan, &evt, K_NO_WAIT)
    → attr_set(PM_SUSPEND) → write 0x01 to reg 0x01
```

**Note:** The Zephyr decoder's q31 encoding (0→0, 1000→INT32_MAX) matches our project's
PM Q31 convention, so no rescaling is needed between `decoder->decode()` output and
`env_sensor_data.q31_value`.

## Data Model

### New `enum sensor_type` Entries

In `lib/sensor_event/include/sensor_event/sensor_event.h`:

| Entry | Physical Range | Q31 Mapping |
|---|---|---|
| `SENSOR_TYPE_PM1_0` | 0–1000 µg/m³ | `pm_ugm3_to_q31(phys)`: `(phys / 1000.0) * INT32_MAX` |
| `SENSOR_TYPE_PM2_5` | 0–1000 µg/m³ | Same Q31 helper |
| `SENSOR_TYPE_PM10` | 0–1000 µg/m³ | Same Q31 helper |

All three share one `pm_ugm3_to_q31()` / `q31_to_pm_ugm3()` pair — same physical unit
and range. The **encode** helper (`pm_ugm3_to_q31`) is used by fake sensors and future
drivers. The **decode** helper (`q31_to_pm_ugm3`) is used by display/MQTT consumers.

The SEN0460 wrapper does **not** call `pm_ugm3_to_q31()` — it takes the `q31_t` value
directly from the Zephyr decoder output (which already encodes to the same range).

### Zephyr Sensor Channel Mapping

In `sen0460_driver.c` (decoder implementation):

| Zephyr `sensor_channel` | Register | Bytes in buffer | Q31 output range |
|---|---|---|---|
| `SENSOR_CHAN_PM_1_0` | 0x05 | buf[0..1] | 0–INT32_MAX (0–1000 µg/m³) |
| `SENSOR_CHAN_PM_2_5` | 0x07 | buf[2..3] | 0–INT32_MAX (0–1000 µg/m³) |
| `SENSOR_CHAN_PM_10` | 0x09 | buf[4..5] | 0–INT32_MAX (0–1000 µg/m³) |

The driver's `sensor_submit_t` reads all 6 bytes in one I2C burst (registers 0x05–0x0A).
The decoder extracts the subset for the requested channel.

## Power Management

### Settle-Sequence Flow

The trigger-driven model requires a timed settle-then-read sequence since the sensor
needs ~10s for fan + laser stabilization after wake:

```
Timer trigger (every 5 min) → sensor_trigger_chan → sen0460_trigger_cb
  → awakes sensor via attr_set(PM_RESUME) [I2C write 0x02 to 0x01]
  → submits k_work_delayable with CONFIG_SEN0460_SENSOR_SETTLE_MS delay
  → returns immediately (non-blocking callback per ADR-004)

After settle delay:
  sen0460_sample_work_fn:
    → sensor_read() → decoder->decode() × 3 → populate + publish env_sensor_data × 3
    → suspends sensor via attr_set(PM_SUSPEND) [I2C write 0x01 to 0x01]
```

### Power State Machine

```
SUSPENDED ←── init, disable(), after each sample cycle
    │
    ▼ trigger arrives (only if enabled)
AWAKENING ←── attr_set(PM_RESUME) + settle timer started
    │
    ▼ settle timer fires
SAMPLING ←── sensor_read + decoder.decode + publish
    │
    ▼ publish complete
SUSPENDED ←── attr_set(PM_SUSPEND)
```

### Interactions with enable/disable

- `sen0460_sensor_enable()`: sets `enabled = true`, wakes the sensor (so it's ready when trigger arrives)
- `sen0460_sensor_disable()`: cancels any pending settle timer, suspends sensor, sets `enabled = false`
- Trigger while disabled: silent drop (existing behavior)
- Trigger while already awakening/sampling: ignored (prevent double-fire)
- `attr_set(PM_SUSPEND/RESUME)` calls gated by `#ifdef CONFIG_PM_DEVICE` with `-ENOSYS` tolerance

### Battery Budget

At 5-minute intervals with 10s settle + ~2s active read:

- 12s @ 100mA + 288s @ 2mA = 1776 mAs per 300s cycle
- Average current: ~5.9mA
- 2000mAh battery: ~14 days

Future optimization: reduce settle time via Kconfig after field testing of actual stabilization.

## Kconfig

```kconfig
menuconfig SEN0460_SENSOR
    bool "SEN0460 PM2.5 sensor (read/decode API)"
    depends on SENSOR
    depends on SENSOR_EVENT
    depends on SENSOR_TRIGGER
    depends on I2C
    select RTIO
    select HW_SENSOR_UTILS
    help
      Driver for DFRobot SEN0460 PM2.5 air quality sensor (I2C).
      Uses Zephyr's read/decode sensor API (sensor_submit_t + sensor_decoder_api).
      Measures PM1.0, PM2.5, PM10 standard concentration.
      Subscribes to sensor_trigger_chan and publishes env_sensor_data events.

if SEN0460_SENSOR

config SEN0460_SENSOR_DEFAULT_UID
    hex "Default sensor UID"
    default 0x0021

config SEN0460_SENSOR_SETTLE_MS
    int "Settle delay after wake (ms)"
    default 10000
    range 0 30000
    help
      Delay between waking the sensor and reading. The internal laser
      and fan need time to stabilize for accurate PM readings.

config SEN0460_SENSOR_SAMPLE_INTERVAL_SEC
    int "Sample interval (seconds)"
    default 300
    range 10 3600
    help
      Interval between measurement cycles. Used by the trigger source
      (sensor_trigger) to configure the timer period. Set at compile-time;
      runtime override via config_cmd_chan is deferred.

config SEN0460_SENSOR_LOG_LEVEL
    int "Log level"
    default 3

module = SEN0460_SENSOR
module-str = sen0460_sensor
source "subsys/logging/Kconfig.template.log_config"

endif # SEN0460_SENSOR
```

## Error Handling

| Scenario | Behavior |
|---|---|
| No SEN0460 DT node / HW absent | Init logs warning, returns 0, driver stays inactive |
| `i2c_is_ready_dt()` fails at init | Same graceful degradation |
| `sensor_submit` fails (I2C NACK) | Complete RTIO SQE with error code; wrapper logs error, skips cycle |
| `sensor_read()` returns error | `LOG_ERR`, skip this cycle, try again next trigger |
| `decoder->decode()` called with unsupported channel | Return `-ENOTSUP` |
| `zbus_chan_pub()` fails | `LOG_WRN`, continue with next channel |
| Trigger during settle/awake | Silent drop (prevent double-fire) |
| `CONFIG_PM_DEVICE=n` | `attr_set(PM_SUSPEND/RESUME)` returns `-ENOSYS`, ignored with warning |

## Test Strategy

1. **native_sim build** with `CONFIG_SEN0460_SENSOR=y` + `CONFIG_RTIO=y` + DT overlay —
   verify compilation and init
2. **Integration test** (pytest, smoke marker): trigger via shell, verify PM2.5
   `env_sensor_data` appears in event log
3. **Sensor registry test:** `sensor_registry list` shows `sen0460` with UID `0x0021`
4. **Power cycle test:** enable → trigger → verify settle delay → verify suspend → disable
5. **PM-disabled build:** verify enable/disable still works without `CONFIG_PM_DEVICE`
6. **Error path test:** trigger without DT node → graceful warning, no crash
7. **Decoder unit test:** feed raw 6-byte buffer, verify decode outputs correct q31 values

App integration is in `apps/outdoor_sensor_node/` only — no changes to `apps/gateway/`.

## ADR Compliance

| ADR | Verdict | Notes |
|-----|---------|-------|
| ADR-002 (zbus as system bus) | Compliant | `ZBUS_LISTENER_DEFINE` in wrapper `.c` |
| ADR-003 (sensor event data model) | Compliant | One event per measurement; flat struct; Q31 encoding |
| ADR-004 (trigger-driven sampling) | Compliant | Subscribe `sensor_trigger_chan`; settle deferred to `k_work_delayable`; no blocking in callback |
| ADR-005 (fake sensor subsystem) | Compliant | Hardware driver coexists; swapped via Kconfig |
| ADR-008 (Kconfig app composition) | Compliant | Self-wired via `SYS_INIT`; no `target_link_libraries()` changes in app CMake |

## Implementation Plan Summary

1. Add `SENSOR_TYPE_PM1_0`, `SENSOR_TYPE_PM2_5`, `SENSOR_TYPE_PM10` to `enum sensor_type`
   + `pm_ugm3_to_q31()`/`q31_to_pm_ugm3()` helpers to `sensor_event.h`
2. Create `lib/sen0460_sensor/src/sen0460_driver.c` — Zephyr sensor driver implementing
   `sensor_submit_t` + `sensor_decoder_api` + `sensor_attr_set_t`
3. Rewrite `lib/sen0460_sensor/src/sen0460_sensor.c` — functional trigger callback,
   settle work item, `sensor_read()` + `decoder->decode()` publish pipeline
4. Update `lib/sen0460_sensor/Kconfig` — add `select RTIO`, settle time, sample interval
5. Update `lib/sen0460_sensor/CMakeLists.txt` — add `sen0460_driver.c`, I2C + RTIO includes
6. Add DT overlay for `dfr,sen0460` in `apps/outdoor_sensor_node/`
7. Add integration tests (smoke marker)
8. Update CLAUDE.md catalog entry (remove "stub" qualification)
9. Remove `[HW-SENSOR]` entry from `docs/backlog.md`
