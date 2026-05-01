# nanopb Migration: pipe_publisher + pipe_transport

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hand-rolled protobuf varint encode/decode in `lib/pipe_publisher` and `lib/pipe_transport` with the nanopb library, using Zephyr's build-time code-generation workflow.

**Architecture:** `proto/sensor_message.proto` is the single source of truth. Each library calls `nanopb_generate_cpp()` directly (with the proto's own directory as RELPATH) so the generated `sensor_message.pb.h` lands in `CMAKE_CURRENT_BINARY_DIR` and is reachable via a plain `#include "sensor_message.pb.h"`. The nanopb C runtime is pulled in as a Zephyr module and linked automatically when `CONFIG_NANOPB=y`.

**Tech Stack:** Zephyr v4.4 + west, nanopb, protoc (protobuf-compiler apt package), nanopb pip package (protoc-gen-nanopb plugin).

---

## File map

| File | Action |
|---|---|
| `.devcontainer/Dockerfile.ci` | Add `protobuf-compiler` (apt) + `nanopb` (pip) |
| `west.yml` | Add `nanopb` to `name-allowlist` |
| `lib/pipe_publisher/CMakeLists.txt` | Add nanopb cmake wiring, replace `zephyr_library_sources` |
| `lib/pipe_publisher/Kconfig` | Add `select NANOPB` |
| `lib/pipe_publisher/src/pipe_publisher.c` | Replace encoder with `pb_encode` |
| `lib/pipe_transport/CMakeLists.txt` | Add nanopb cmake wiring, replace `zephyr_library_sources` |
| `lib/pipe_transport/Kconfig` | Add `select NANOPB` |
| `lib/pipe_transport/src/pipe_transport.c` | Replace decoder with `pb_decode` |

---

## Task 1: Install protoc + nanopb in the current dev environment

**Files:**
- Modify: `.devcontainer/Dockerfile.ci`

- [ ] **Step 1: Install protoc and nanopb in the running container**

```bash
sudo apt-get install -y protobuf-compiler
/home/zephyr/.venv/bin/pip install nanopb
```

- [ ] **Step 2: Verify both tools are available**

```bash
protoc --version
/home/zephyr/.venv/bin/python -c "import nanopb; print('nanopb ok')"
```

Expected: `libprotoc 3.x.x` and `nanopb ok`.

- [ ] **Step 3: Update Dockerfile.ci to document the new deps**

Replace the existing apt block and pip block:

```dockerfile
# CI image for weather-station GitHub Actions.
# Extends the Zephyr SDK base with coverage and diagram-rendering tooling.
#
# This file documents the full CI environment.  It is not built automatically
# during CI — the workflow installs the lightweight extras inline so no
# separate image publish step is required.  To build and push manually:
#
#   docker build -t ghcr.io/tobiwan88/weather-station-ci:latest \
#     -f .devcontainer/Dockerfile.ci .devcontainer/
#   docker push ghcr.io/tobiwan88/weather-station-ci:latest

FROM ghcr.io/tobiwan88/zephyr_docker:arm

USER root

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    nodejs npm \
    protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

USER zephyr

# Coverage + lint tooling inside the existing venv
RUN /home/zephyr/.venv/bin/pip install --no-cache-dir \
    pre-commit \
    gcovr \
    nanopb

# Mermaid CLI for diagram rendering
RUN npm install -g @mermaid-js/mermaid-cli

ENV PATH="/home/zephyr/.venv/bin:/home/zephyr/.local/bin:${PATH}"
```

- [ ] **Step 4: Commit Dockerfile.ci**

```bash
git add .devcontainer/Dockerfile.ci
git commit -m "build(ci): add protobuf-compiler and nanopb to devcontainer"
```

---

## Task 2: Add nanopb west module and pull source

**Files:**
- Modify: `west.yml`

- [ ] **Step 1: Add `nanopb` to the west.yml allowlist**

Edit `west.yml`. The `name-allowlist` block currently ends at `segger`. Add `nanopb`:

```yaml
manifest:
  version: "0.13"

  self:
    path: weather-station

  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos

  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: v4.4.0
      import:
        name-allowlist:
          - cmsis_6
          - hal_nordic
          - mbedtls
          - lvgl
          - loramac-node
          - segger
          - nanopb
```

- [ ] **Step 2: Run west update to fetch the nanopb source**

```bash
west update --narrow
```

Expected: west fetches and checks out the nanopb module. No errors.

- [ ] **Step 3: Verify nanopb source is present**

```bash
ls /home/zephyr/workspace/modules/lib/nanopb/pb.h
```

Expected: file exists. If the path is different, check with `west list nanopb`.

- [ ] **Step 4: Commit**

```bash
git add west.yml
git commit -m "build: add nanopb to west allowlist"
```

---

## Task 3: Wire pipe_publisher and replace encoder

**Files:**
- Modify: `lib/pipe_publisher/CMakeLists.txt`
- Modify: `lib/pipe_publisher/Kconfig`
- Modify: `lib/pipe_publisher/src/pipe_publisher.c`

- [ ] **Step 1: Replace lib/pipe_publisher/CMakeLists.txt**

```cmake
# SPDX-License-Identifier: Apache-2.0
if(CONFIG_PIPE_PUBLISHER)
  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)

  zephyr_library()

  # Generate sensor_message.pb.c/.pb.h into CMAKE_CURRENT_BINARY_DIR so that
  # #include "sensor_message.pb.h" resolves without path gymnastics.
  set(NANOPB_GENERATE_CPP_STANDALONE OFF)
  set(_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
  nanopb_generate_cpp(_pb_srcs _pb_hdrs
    RELPATH ${_proto_dir}
    ${_proto_dir}/sensor_message.proto)
  target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_pb_srcs} ${_pb_hdrs})
  string(MD5 _hash "${_pb_hdrs}")
  add_custom_target(_pipe_pub_proto_${_hash} DEPENDS ${_pb_hdrs})
  add_dependencies(nanopb_generated_headers _pipe_pub_proto_${_hash})
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} nanopb_generated_headers)

  zephyr_library_sources(src/pipe_publisher.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
```

- [ ] **Step 2: Add `select NANOPB` to lib/pipe_publisher/Kconfig**

The full file after the edit:

```kconfig
# SPDX-License-Identifier: Apache-2.0
menuconfig PIPE_PUBLISHER
	bool "FIFO pipe publisher (sensor-node side, native_sim only)"
	depends on BOARD_NATIVE_SIM
	depends on ZBUS
	depends on SENSOR_EVENT
	select NANOPB
	help
	  Subscribes to sensor_event_chan and writes each env_sensor_data
	  event as a length-prefixed protobuf SensorReading to a POSIX FIFO.
	  For native_sim integration testing only.

if PIPE_PUBLISHER

config PIPE_PUBLISHER_FIFO_PATH
	string "FIFO path"
	default "/tmp/ws-node-0"

config PIPE_PUBLISHER_THREAD_STACK_SIZE
	int "Subscriber thread stack size (bytes)"
	default 1024
	range 512 4096

config PIPE_PUBLISHER_THREAD_PRIORITY
	int "Subscriber thread priority"
	default 5
	range 0 NUM_PREEMPT_PRIORITIES

module = PIPE_PUBLISHER
module-str = PIPE_PUBLISHER
source "subsys/logging/Kconfig.template.log_config"

endif # PIPE_PUBLISHER
```

- [ ] **Step 3: Replace lib/pipe_publisher/src/pipe_publisher.c**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file pipe_publisher.c
 * @brief Sensor-node side FIFO publisher. Subscribes to sensor_event_chan
 *        and writes length-prefixed protobuf SensorReading frames to a POSIX
 *        FIFO for native_sim integration testing.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <pb_encode.h>
#include <sensor_event/sensor_event.h>
#include "sensor_message.pb.h"

LOG_MODULE_REGISTER(pipe_publisher, CONFIG_PIPE_PUBLISHER_LOG_LEVEL);

ZBUS_CHAN_DECLARE(sensor_event_chan);
ZBUS_SUBSCRIBER_DEFINE(pipe_pub_sub, 8);

/* 2-byte LE length prefix + max nanopb-encoded SensorReading */
static int s_fifo_fd = -1;
static uint8_t s_tx_buf[SensorReading_size + 2];

/* --------------------------------------------------------------------------
 * FIFO open with retry
 * -------------------------------------------------------------------------- */

static void ensure_open(void)
{
	if (s_fifo_fd >= 0) {
		return;
	}

	/* O_NONBLOCK so open() does not block waiting for the read end. */
	s_fifo_fd = open(CONFIG_PIPE_PUBLISHER_FIFO_PATH, O_WRONLY | O_NONBLOCK);
	if (s_fifo_fd < 0) {
		/* Read end not yet open — caller retries later. */
		LOG_DBG("fifo open pending (errno=%d)", errno);
	}
}

/* --------------------------------------------------------------------------
 * Subscriber thread
 * -------------------------------------------------------------------------- */

static void pipe_pub_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	const struct zbus_channel *chan;

	while (true) {
		int rc = zbus_sub_wait(&pipe_pub_sub, &chan, K_FOREVER);

		if (rc != 0) {
			LOG_ERR("zbus_sub_wait: %d", rc);
			continue;
		}

		struct env_sensor_data evt;

		if (zbus_chan_read(&sensor_event_chan, &evt, K_NO_WAIT) != 0) {
			continue;
		}

		/* Retry open if FIFO not yet connected to a reader. */
		if (s_fifo_fd < 0) {
			ensure_open();
			if (s_fifo_fd < 0) {
				k_sleep(K_MSEC(100));
				ensure_open();
				if (s_fifo_fd < 0) {
					LOG_WRN("fifo not open, dropping event uid=0x%08x",
						evt.sensor_uid);
					continue;
				}
			}
		}

		SensorReading msg = {
			.sensor_uid   = evt.sensor_uid,
			.sensor_type  = (uint32_t)evt.type,
			.q31_value    = evt.q31_value,
			.timestamp_ms = evt.timestamp_ms,
		};

		uint8_t payload[SensorReading_size];
		pb_ostream_t ostream = pb_ostream_from_buffer(payload, sizeof(payload));

		if (!pb_encode(&ostream, SensorReading_fields, &msg)) {
			LOG_ERR("pb_encode: %s", PB_GET_ERROR(&ostream));
			continue;
		}

		size_t plen = ostream.bytes_written;

		/* 2-byte little-endian length prefix. */
		s_tx_buf[0] = (uint8_t)(plen & 0xFF);
		s_tx_buf[1] = (uint8_t)((plen >> 8) & 0xFF);
		memcpy(s_tx_buf + 2, payload, plen);

		ssize_t written = write(s_fifo_fd, s_tx_buf, plen + 2);

		if (written < 0) {
			LOG_WRN("fifo write failed (errno=%d), closing", errno);
			close(s_fifo_fd);
			s_fifo_fd = -1;
		} else {
			LOG_DBG("published uid=0x%08x type=%u q31=%d (%zu bytes)",
				evt.sensor_uid, (unsigned)evt.type, evt.q31_value, plen);
		}
	}
}

K_THREAD_DEFINE(pipe_pub_tid, CONFIG_PIPE_PUBLISHER_THREAD_STACK_SIZE, pipe_pub_thread, NULL, NULL,
		NULL, CONFIG_PIPE_PUBLISHER_THREAD_PRIORITY, 0, 0);

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 94 — subscribe to sensor_event_chan
 * -------------------------------------------------------------------------- */

static int pipe_publisher_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &pipe_pub_sub, K_NO_WAIT);

	if (rc != 0) {
		LOG_ERR("add obs sensor_event_chan: %d", rc);
		return rc;
	}

	LOG_INF("pipe_publisher: init done (fifo=%s)", CONFIG_PIPE_PUBLISHER_FIFO_PATH);
	return 0;
}

SYS_INIT(pipe_publisher_init, APPLICATION, 94);
```

- [ ] **Step 4: Build the sensor-node to verify**

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west build -b native_sim/native/64 \
  --build-dir /home/zephyr/workspace/build/native_sim_native_64/sensor_node \
  apps/sensor-node
```

Expected: build succeeds, no errors. If `NANOPB_SRC_ROOT_FOLDER not set` appears, check `west list nanopb` to confirm the nanopb module was fetched.

- [ ] **Step 5: Commit**

```bash
git add lib/pipe_publisher/CMakeLists.txt \
        lib/pipe_publisher/Kconfig \
        lib/pipe_publisher/src/pipe_publisher.c
git commit -m "feat(pipe-publisher): migrate encoder to nanopb pb_encode"
```

---

## Task 4: Wire pipe_transport and replace decoder

**Files:**
- Modify: `lib/pipe_transport/CMakeLists.txt`
- Modify: `lib/pipe_transport/Kconfig`
- Modify: `lib/pipe_transport/src/pipe_transport.c`

- [ ] **Step 1: Replace lib/pipe_transport/CMakeLists.txt**

```cmake
# SPDX-License-Identifier: Apache-2.0
if(CONFIG_PIPE_TRANSPORT)
  list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
  include(nanopb)

  zephyr_library()

  # Generate sensor_message.pb.c/.pb.h into CMAKE_CURRENT_BINARY_DIR so that
  # #include "sensor_message.pb.h" resolves without path gymnastics.
  set(NANOPB_GENERATE_CPP_STANDALONE OFF)
  set(_proto_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../proto)
  nanopb_generate_cpp(_pb_srcs _pb_hdrs
    RELPATH ${_proto_dir}
    ${_proto_dir}/sensor_message.proto)
  target_include_directories(${ZEPHYR_CURRENT_LIBRARY} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_sources(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_pb_srcs} ${_pb_hdrs})
  string(MD5 _hash "${_pb_hdrs}")
  add_custom_target(_pipe_xport_proto_${_hash} DEPENDS ${_pb_hdrs})
  add_dependencies(nanopb_generated_headers _pipe_xport_proto_${_hash})
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} nanopb_generated_headers)

  zephyr_library_sources(src/pipe_transport.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)
endif()
```

- [ ] **Step 2: Add `select NANOPB` to lib/pipe_transport/Kconfig**

The full file after the edit:

```kconfig
# SPDX-License-Identifier: Apache-2.0
menuconfig PIPE_TRANSPORT
	bool "FIFO pipe transport (gateway side, native_sim only)"
	depends on BOARD_NATIVE_SIM
	select REMOTE_SENSOR
	select NANOPB
	help
	  Implements the remote_transport vtable reading from a POSIX FIFO.
	  Decodes SensorReading protobuf frames and publishes to sensor_event_chan.
	  For native_sim integration testing only.

if PIPE_TRANSPORT

config PIPE_TRANSPORT_FIFO_PATH
	string "FIFO path"
	default "/tmp/ws-node-0"

config PIPE_TRANSPORT_THREAD_STACK_SIZE
	int "Reader thread stack size (bytes)"
	default 1024
	range 512 4096

config PIPE_TRANSPORT_THREAD_PRIORITY
	int "Reader thread priority"
	default 6
	range 0 NUM_PREEMPT_PRIORITIES

module = PIPE_TRANSPORT
module-str = PIPE_TRANSPORT
source "subsys/logging/Kconfig.template.log_config"

endif # PIPE_TRANSPORT
```

- [ ] **Step 3: Replace lib/pipe_transport/src/pipe_transport.c**

```c
/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file pipe_transport.c
 * @brief Gateway-side FIFO transport. Creates the POSIX FIFO, opens it for
 *        reading, decodes length-prefixed protobuf SensorReading frames, and
 *        publishes measurements to sensor_event_chan via remote_sensor_publish_data().
 *
 *        Discovery is lazy: the first frame from a new UID triggers a
 *        REMOTE_DISCOVERY_FOUND announce so remote_sensor_manager can register
 *        it before data starts flowing.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>

#include <pb_decode.h>
#include <remote_sensor/remote_sensor.h>
#include <sensor_event/sensor_event.h>
#include "sensor_message.pb.h"

LOG_MODULE_REGISTER(pipe_transport, CONFIG_PIPE_TRANSPORT_LOG_LEVEL);

/* --------------------------------------------------------------------------
 * Per-UID seen table for lazy discovery
 * -------------------------------------------------------------------------- */

static uint32_t s_seen_uids[CONFIG_REMOTE_SENSOR_MAX_PEERS];

static bool uid_is_seen(uint32_t uid)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (s_seen_uids[i] == uid) {
			return true;
		}
	}
	return false;
}

static void uid_mark_seen(uint32_t uid)
{
	for (int i = 0; i < CONFIG_REMOTE_SENSOR_MAX_PEERS; i++) {
		if (s_seen_uids[i] == 0) {
			s_seen_uids[i] = uid;
			return;
		}
	}
	LOG_WRN("seen_uids table full, uid=0x%08x not tracked", uid);
}

/* --------------------------------------------------------------------------
 * Transport vtable (pipe is receive-only; no scan, no trigger)
 * -------------------------------------------------------------------------- */

static int pipe_peer_add(const struct remote_transport *t, const uint8_t *peer_addr,
			 size_t addr_len, uint32_t uid)
{
	ARG_UNUSED(t);
	ARG_UNUSED(peer_addr);
	ARG_UNUSED(addr_len);
	ARG_UNUSED(uid);
	return 0;
}

static int pipe_peer_remove(const struct remote_transport *t, uint32_t uid)
{
	ARG_UNUSED(t);
	ARG_UNUSED(uid);
	return 0;
}

REMOTE_TRANSPORT_DEFINE(pipe_remote_transport, {
						       .name = "pipe",
						       .proto = REMOTE_TRANSPORT_PROTO_PIPE,
						       .caps = 0,
						       .scan_start = NULL,
						       .scan_stop = NULL,
						       .peer_add = pipe_peer_add,
						       .peer_remove = pipe_peer_remove,
						       .send_trigger = NULL,
					       });

/* --------------------------------------------------------------------------
 * Reader thread — blocking FIFO read loop
 * -------------------------------------------------------------------------- */

static void pipe_reader_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		/* open() blocks until the write end is opened (correct FIFO semantics). */
		int fd = open(CONFIG_PIPE_TRANSPORT_FIFO_PATH, O_RDONLY);

		if (fd < 0) {
			LOG_WRN("fifo open failed (errno=%d), retrying", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("pipe_transport: fifo opened for reading");

		while (true) {
			/* Read 2-byte little-endian length prefix. */
			uint8_t len_buf[2];
			size_t len_received = 0;
			ssize_t n = 0;

			while (len_received < sizeof(len_buf)) {
				n = read(fd, len_buf + len_received,
					 sizeof(len_buf) - len_received);
				if (n <= 0) {
					break;
				}
				len_received += (size_t)n;
			}

			if (n == 0) {
				LOG_INF("pipe_transport: writer disconnected, reopening");
				break;
			}
			if (n < 0) {
				LOG_WRN("fifo read error (errno=%d)", errno);
				break;
			}

			uint16_t payload_len =
				(uint16_t)len_buf[0] | ((uint16_t)len_buf[1] << 8);

			if (payload_len > SensorReading_size) {
				LOG_WRN("payload_len=%u exceeds max=%d, discarding frame",
					payload_len, SensorReading_size);
				break;
			}

			uint8_t payload[SensorReading_size];
			size_t received = 0;

			while (received < payload_len) {
				n = read(fd, payload + received, payload_len - received);
				if (n <= 0) {
					break;
				}
				received += (size_t)n;
			}

			if (received < payload_len) {
				LOG_WRN("truncated payload (got=%zu want=%u), discarding",
					received, payload_len);
				break;
			}

			SensorReading msg = SensorReading_init_zero;
			pb_istream_t istream = pb_istream_from_buffer(payload, payload_len);

			if (!pb_decode(&istream, SensorReading_fields, &msg)) {
				LOG_WRN("pb_decode: %s", PB_GET_ERROR(&istream));
				continue;
			}

			uint32_t uid  = msg.sensor_uid;
			uint32_t type = msg.sensor_type;
			int32_t q31   = msg.q31_value;

			/* Announce new UIDs before publishing data. */
			if (!uid_is_seen(uid)) {
				struct remote_discovery_event disc = {
					.action = REMOTE_DISCOVERY_FOUND,
					.proto = REMOTE_TRANSPORT_PROTO_PIPE,
					.sensor_type = (enum sensor_type)type,
					.suggested_uid = uid,
					.peer_addr_len = 4,
				};

				memcpy(disc.peer_addr, &uid, sizeof(uid));
				snprintf(disc.suggested_label, sizeof(disc.suggested_label),
					 "pipe-%08x", uid);

				int rc = remote_sensor_announce_disc(&disc);

				if (rc == 0) {
					uid_mark_seen(uid);
					/* Allow remote_sensor_manager to process discovery
					 * before data arrives. */
					k_sleep(K_MSEC(100));
				} else {
					LOG_WRN("announce_disc uid=0x%08x failed: %d", uid, rc);
				}
			}

			int rc = remote_sensor_publish_data(uid, (enum sensor_type)type, q31);

			if (rc != 0) {
				LOG_WRN("publish_data uid=0x%08x failed: %d", uid, rc);
			} else {
				LOG_DBG("received uid=0x%08x type=%u q31=%d", uid, type, q31);
			}
		}

		close(fd);
	}
}

K_THREAD_DEFINE(pipe_reader_tid, CONFIG_PIPE_TRANSPORT_THREAD_STACK_SIZE, pipe_reader_thread, NULL,
		NULL, NULL, CONFIG_PIPE_TRANSPORT_THREAD_PRIORITY, 0, 0);

/* --------------------------------------------------------------------------
 * SYS_INIT APPLICATION 93 — create FIFO before publisher connects
 * -------------------------------------------------------------------------- */

static int pipe_transport_init(void)
{
	int rc = mkfifo(CONFIG_PIPE_TRANSPORT_FIFO_PATH, 0600);

	if (rc != 0 && errno != EEXIST) {
		LOG_ERR("mkfifo '%s' failed (errno=%d)", CONFIG_PIPE_TRANSPORT_FIFO_PATH, errno);
		return -errno;
	}

	LOG_INF("pipe_transport: init done (fifo=%s)", CONFIG_PIPE_TRANSPORT_FIFO_PATH);
	return 0;
}

SYS_INIT(pipe_transport_init, APPLICATION, 93);
```

- [ ] **Step 4: Run the full build gate**

```bash
/build-and-test
```

Expected: all builds and Twister tests pass. If `sensor_message.pb.h: No such file` appears, the RELPATH computation is off — check that `${CMAKE_CURRENT_SOURCE_DIR}/../../proto/sensor_message.proto` resolves to the actual proto file with `message(STATUS ...)` in the CMakeLists.

- [ ] **Step 5: Commit**

```bash
git add lib/pipe_transport/CMakeLists.txt \
        lib/pipe_transport/Kconfig \
        lib/pipe_transport/src/pipe_transport.c
git commit -m "feat(pipe-transport): migrate decoder to nanopb pb_decode"
```

---

## Task 5: Integration tests

**Files:** none

- [ ] **Step 1: Run the integration test suite**

```bash
/run-integration-tests
```

Expected: all tests pass. The wire framing (2-byte LE prefix + payload) is unchanged, so the gateway's FIFO reader will decode frames encoded by the sensor-node identically to before.

- [ ] **Step 2: If any test fails, check the debug output**

Run with verbose logging to see the frame exchange:

```bash
/run-integration-tests -k pipe
```

Check `handler.log` in the Twister output for `pb_encode` / `pb_decode` error messages from the embedded side.

- [ ] **Step 3: Open a PR**

```bash
/open-pr
```
