# Sensor-Node → Gateway Communication: System Test Design

**Date:** 2026-04-29
**Status:** Approved

## Goal

Add a system-level integration test that boots both the `gateway` and `sensor-node`
binaries and verifies end-to-end data flow between them via a POSIX named pipe (FIFO).
The existing `fake_remote_sensor` is kept for unit/smoke tests. This test covers the
real sensor-node data pipeline: sensor reading → protobuf encode → FIFO → gateway
decode → `sensor_event_chan` → HTTP `/api/data`.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│ pytest / Twister                                                         │
│                                                                          │
│  Twister DUT                     SensorNodeHarness                      │
│  gateway.exe ◄────────────────── sensor-node.exe                        │
│    pipe_transport                  pipe_publisher                        │
│         │                               │                               │
│         └──── FIFO /tmp/ws-node-0 ──────┘                               │
│                                                                          │
│  ShellHarness ──► gateway shell          (trigger, verify registry)      │
│  SensorNodeHarness ──► sensor-node shell  (set values, trigger)         │
│  HttpHarness ──► GET /api/data           (assert readings arrived)       │
└─────────────────────────────────────────────────────────────────────────┘
```

**New components:**

| Component | Location | Role |
|---|---|---|
| `proto/sensor_message.proto` | repo root | Canonical schema shared by C and Python |
| `lib/pipe_publisher` | sensor-node only | Subscribes to `sensor_event_chan`, encodes protobuf + 2-byte LE length prefix, writes to FIFO |
| `lib/pipe_transport` | gateway only | Implements `remote_transport` vtable; reads FIFO, decodes protobuf, calls `remote_sensor_publish_data()` |
| `SensorNodeHarness` | `tests/integration/pytest/harnesses/` | Spawns `sensor-node.exe`, manages shell, tears down cleanly |
| `test_sensor_node_gateway.py` | `tests/integration/pytest/` | New system test file; marker: `system` |

**What does NOT change:** `fake_remote_sensor`, all existing integration tests, gateway's
`sensor_event_chan` consumers, MQTT publisher, HTTP dashboard. `pipe_transport` is one
more entry in the `remote_transport` vtable iteration — transparent to all existing
consumers.

## Wire Protocol

**Schema — `proto/sensor_message.proto`:**

```proto
syntax = "proto3";

message SensorReading {
  uint32 sensor_uid   = 1;
  uint32 sensor_type  = 2;   // maps to enum sensor_type values
  int32  q31_value    = 3;   // already Q31-encoded
  int64  timestamp_ms = 4;   // k_uptime_get() on sensor-node
}
```

One message per physical measurement (rule: one event = one physical measurement).
Fields map 1:1 to `env_sensor_data` — no conversion logic in the gateway transport.

**Framing:** 2-byte little-endian length prefix followed by hand-rolled protobuf varint
encoding of the `SensorReading` fields. The encoding mirrors the standard protobuf
wire format (field tag + varint value pairs) without using nanopb or any generated
code on the C side. Max payload is bounded at compile time by `PAYLOAD_MAX` (64 bytes).

**Encoding:** `lib/pipe_publisher` contains a `varint_encode()` / `sensor_reading_encode()`
and `lib/pipe_transport` contains a matching `varint_decode()` / `sensor_reading_decode()`.
Both are self-contained — no external protobuf library is required in the firmware build.

**Python side:** The test harness uses the standard `protobuf` package with the
generated `sensor_message_pb2` module (generated from the same `.proto` and committed
to the repo) if it needs to inject or decode messages directly.

## `lib/pipe_publisher` (sensor-node side)

**Responsibility:** Subscribe to `sensor_event_chan`; on each `env_sensor_data` event,
encode as `SensorReading` protobuf and write 2-byte LE length prefix + payload to FIFO.

- **FIFO path:** `CONFIG_PIPE_PUBLISHER_FIFO_PATH` (default `"/tmp/ws-node-0"`).
- **Open mode:** `O_WRONLY | O_NONBLOCK`, opened lazily from the subscriber thread
  (`ensure_open()`) on the first publish attempt. `SYS_INIT APPLICATION 94` only
  registers the zbus observer; the FIFO is not opened at boot. If the gateway read end
  is not yet open, `open()` returns `ENXIO` — the publisher retries on the next event.
  Avoids boot-time deadlock where both sides block waiting for the other.
- **Write path:** Runs in a `ZBUS_SUBSCRIBER_DEFINE` thread (not a listener) so it can
  block briefly on `write()` without stalling the zbus fast path. Static
  `uint8_t tx_buf[PAYLOAD_MAX + 2]` — no heap.
- **Dropped writes:** `write()` with `O_NONBLOCK`; if the pipe is full or the read end
  is gone, the event is dropped with `LOG_WRN`. No retry — acceptable for a test channel.
- **Enabled by:** `CONFIG_PIPE_PUBLISHER=y`. Off by default; added to
  `apps/sensor-node/prj.conf`.
- **Thread stack:** `CONFIG_PIPE_PUBLISHER_THREAD_STACK_SIZE` (default 1024 bytes).

## `lib/pipe_transport` (gateway side)

**Responsibility:** Implement the `remote_transport` vtable. On `scan_start`, announce
discovery for all configured sensor-node UIDs. Run a reader thread that reads the FIFO,
decodes protobuf frames, and calls `remote_sensor_publish_data()`.

- **FIFO path:** `CONFIG_PIPE_TRANSPORT_FIFO_PATH` (default `"/tmp/ws-node-0"`, must
  match publisher). Created by the gateway at `SYS_INIT` with `mkfifo()`; `EEXIST`
  is ignored.
- **Reader thread:** `K_THREAD_DEFINE` thread opens FIFO with `open(O_RDONLY)` — this
  blocks until the sensor-node opens the write end, so no polling loop is needed for
  the initial connection. Then calls blocking `read()` in a loop: read 2-byte LE length
  (looping until both bytes arrive), read payload (same loop pattern), decode with
  `sensor_reading_decode()`, call `remote_sensor_publish_data()`. On EOF
  (`read()` returns 0, i.e. writer closed), close fd and retry `open()`. Note: do NOT
  use `zsock_poll()` here — the `zsock_*` constraint in CLAUDE.md applies to network
  sockets only; FIFOs are host-OS file descriptors on `native_sim` and use POSIX I/O.
- **Discovery:** Lazy — the first frame from a new sensor UID triggers a
  `REMOTE_DISCOVERY_FOUND` event via `remote_sensor_announce_disc()`. No `scan_start`
  involvement; capability flags: none (`caps = 0`).
- **No `send_trigger`:** Pipe is unidirectional (node → gateway).
- **Protocol enum:** `REMOTE_TRANSPORT_PROTO_PIPE = 4` — added after `THREAD = 3`,
  before `FAKE = 15` in `remote_sensor.h`. Values are stored in settings and must never
  be renumbered.
- **Enabled by:** `CONFIG_PIPE_TRANSPORT=y`. Off by default; enabled via integration
  test build overlay only — never in standard gateway `prj.conf`.

## Test Harness

**`SensorNodeHarness`** (`tests/integration/pytest/harnesses/sensor_node_harness.py`):

- Spawns `sensor-node.exe` via `subprocess.Popen` using path from env var
  `SENSOR_NODE_EXE`.
- Wraps the sensor-node UART with a shell interface: `trigger_all()`, `set_temp(uid, mdegc)`, etc.
- `wait_for_ready()` polls until the shell prompt is visible.

**Fixture in `conftest.py`:**

```python
@pytest.fixture()
def sensor_node_harness():
    fifo = "/tmp/ws-node-0"   # fixed default; both Kconfig defaults agree
    exe = os.environ.get("SENSOR_NODE_EXE", "")
    if not exe:
        pytest.skip("SENSOR_NODE_EXE not set")
    harness = SensorNodeHarness(exe, fifo_path=fifo)
    harness.start()
    harness.wait_for_ready()
    yield harness
    harness.stop()
```

**System tests — `test_sensor_node_gateway.py`** (marker: `system`):

| Test | What it verifies |
|---|---|
| `test_sensor_node_discovered` | After boot, gateway registers sensor-node UIDs in registry (shell `sensor_registry list`) |
| `test_sensor_node_reading_in_http` | Trigger sensor-node → reading appears in gateway `/api/data` within timeout |
| `test_sensor_node_value_set_reflected` | Set sensor-node temp via shell, trigger, assert new value in `/api/data` |

**FIFO path agreement:** Default is `"/tmp/ws-node-0"` in both `CONFIG_PIPE_PUBLISHER_FIFO_PATH`
and `CONFIG_PIPE_TRANSPORT_FIFO_PATH`. For the first iteration this fixed path is used.
Parallel test run isolation (using `tmp_path`) requires a Kconfig overlay mechanism and
is deferred to a follow-up.

**`testcase.yaml` addition:** New entry with `CONFIG_PIPE_TRANSPORT=y` overlay for the
gateway build. Sensor-node is built separately; `SENSOR_NODE_EXE` points to its artifact.
CI runs this only when both artifacts are present.

## Error Handling

| Scenario | Behavior |
|---|---|
| Gateway starts before sensor-node opens FIFO write end | Reader thread `open(O_RDONLY\|O_NONBLOCK)` returns `ENXIO` — retries with `k_sleep(K_MSEC(100))` |
| Sensor-node crashes mid-test | `read()` returns 0 (EOF) → log `WRN`, close fd, retry open loop |
| Truncated protobuf frame | `pb_decode` returns false → log `WRN`, discard frame, continue |
| FIFO full (publisher writes faster than reader reads) | `write()` returns `EAGAIN` → drop event, `LOG_WRN` |
| `SENSOR_NODE_EXE` not set | `sensor_node_harness` fixture calls `pytest.skip(...)` |

## Out of Scope (first iteration)

- Bidirectional communication (trigger forwarding gateway→node)
- Multiple concurrent sensor-node processes / multiple FIFOs
- Real wireless transport (BLE/LoRa) — `pipe_transport` is `native_sim`-only, never
  enabled in hardware builds
- Persistence across reboots (`CONFIG_REMOTE_SENSOR_PERSIST` off for pipe transport)
- Parallel test run FIFO isolation via `tmp_path`
