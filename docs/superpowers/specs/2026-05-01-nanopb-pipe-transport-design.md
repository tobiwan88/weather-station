# nanopb migration: pipe_publisher + pipe_transport

**Date:** 2026-05-01
**Branch:** feat/sensor-node-pipe-transport
**Scope:** Replace hand-rolled varint encode/decode in `lib/pipe_publisher` and `lib/pipe_transport` with the nanopb library, using Zephyr's build-time code-generation workflow.

---

## Background

`pipe_publisher.c` (sensor-node encoder) and `pipe_transport.c` (gateway decoder) both contain a manual protobuf varint implementation that encodes/decodes the four-field `SensorReading` message. A `.proto` file already exists at `proto/sensor_message.proto`. nanopb is available as a Zephyr module but is not yet wired up in this project.

---

## Goal

- Remove all hand-rolled varint/field logic from both libraries.
- Use nanopb `pb_encode` / `pb_decode` against generated `.pb.c`/`.pb.h` files.
- Generation happens at build time via Zephyr's `zephyr_nanopb_sources()` CMake helper.
- Wire framing (2-byte LE length prefix) is unchanged.

---

## Tool dependencies

### Dockerfile.ci

Add two packages to the devcontainer image (`.devcontainer/Dockerfile.ci`):

```dockerfile
RUN apt-get install -y protobuf-compiler \
 && pip install nanopb
```

- `protobuf-compiler` provides `protoc`.
- `nanopb` (pip) provides the `protoc-gen-nanopb` plugin that Zephyr's `FindNanopb.cmake` invokes.

### west.yml

Add `nanopb` to the `name-allowlist` so `west update` checks out the nanopb C runtime (pb_encode.c, pb_decode.c, pb.h):

```yaml
name-allowlist:
  - cmsis_6
  - hal_nordic
  - mbedtls
  - lvgl
  - loramac-node
  - segger
  - nanopb      # new
```

---

## Build system changes

Both library `CMakeLists.txt` files follow the same pattern. `nanopb.cmake` has an `include_guard(GLOBAL)`, so including it from two libraries is safe.

### `lib/pipe_publisher/CMakeLists.txt`

```cmake
if(CONFIG_PIPE_PUBLISHER)
  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)

  zephyr_library()
  zephyr_nanopb_sources(${ZEPHYR_CURRENT_LIBRARY}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/sensor_message.proto)
  zephyr_library_sources(src/pipe_publisher.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
```

### `lib/pipe_transport/CMakeLists.txt`

Identical pattern, same proto path.

`zephyr_nanopb_sources()` adds the generated headers to the library's public include path and adds `sensor_message.pb.c` as a source. Each library compiles the proto independently into its own CMake binary directory — no sharing needed.

### Kconfig (both libraries)

Add `select NANOPB` inside the `menuconfig` block so `CONFIG_NANOPB=y` is forced on automatically whenever either library is enabled. No manual `prj.conf` entry required.

---

## C code changes

### `pipe_publisher.c` — encoder

**Remove:**
- `varint_encode()`
- `sensor_reading_encode()`
- `PAYLOAD_MAX`, `TX_BUF_SIZE` constants

**Add includes:**
```c
#include <pb_encode.h>
#include "sensor_message.pb.h"
```

**Replace encode block:**
```c
uint8_t payload[SensorReading_size];
SensorReading msg = {
    .sensor_uid   = evt.sensor_uid,
    .sensor_type  = (uint32_t)evt.type,
    .q31_value    = evt.q31_value,
    .timestamp_ms = evt.timestamp_ms,
};
pb_ostream_t ostream = pb_ostream_from_buffer(payload, sizeof(payload));
if (!pb_encode(&ostream, SensorReading_fields, &msg)) {
    LOG_ERR("pb_encode: %s", PB_GET_ERROR(&ostream));
    continue;
}
size_t plen = ostream.bytes_written;
```

The static `s_tx_buf` becomes `uint8_t[SensorReading_size + 2]`.

### `pipe_transport.c` — decoder

**Remove:**
- `varint_decode()`
- `sensor_reading_decode()`
- `PAYLOAD_MAX` constant (replaced by `SensorReading_size`)

**Add includes:**
```c
#include <pb_decode.h>
#include "sensor_message.pb.h"
```

**Replace decode block:**
```c
SensorReading msg = SensorReading_init_zero;
pb_istream_t istream = pb_istream_from_buffer(payload, payload_len);
if (!pb_decode(&istream, SensorReading_fields, &msg)) {
    LOG_WRN("pb_decode: %s", PB_GET_ERROR(&istream));
    continue;
}
// use msg.sensor_uid, msg.sensor_type, msg.q31_value, msg.timestamp_ms
```

`PAYLOAD_MAX` in the reader thread is replaced by `SensorReading_size` for the frame-size guard.

---

## Files changed

| File | Change |
|---|---|
| `.devcontainer/Dockerfile.ci` | Add `protobuf-compiler` + `pip install nanopb` |
| `west.yml` | Add `nanopb` to allowlist |
| `lib/pipe_publisher/CMakeLists.txt` | Add nanopb include + `zephyr_nanopb_sources()` |
| `lib/pipe_publisher/Kconfig` | Add `select NANOPB` |
| `lib/pipe_publisher/src/pipe_publisher.c` | Replace varint encoder with `pb_encode` |
| `lib/pipe_transport/CMakeLists.txt` | Add nanopb include + `zephyr_nanopb_sources()` |
| `lib/pipe_transport/Kconfig` | Add `select NANOPB` |
| `lib/pipe_transport/src/pipe_transport.c` | Replace varint decoder with `pb_decode` |

---

## What does not change

- The wire framing protocol: 2-byte little-endian length prefix + payload bytes. The gateway's Python/pytest reader is unaffected.
- `proto/sensor_message.proto` itself.
- All other logic in both files (FIFO open/retry, discovery, zbus subscription, thread definitions).
