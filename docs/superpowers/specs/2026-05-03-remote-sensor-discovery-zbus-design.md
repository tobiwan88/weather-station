# Design: Move Remote Sensor Discovery to zbus + Workqueue

**Date:** 2026-05-03
**Status:** Approved
**Author:** opencode

## Problem

The `remote_sensor_manager` uses a dedicated thread with a 50ms timeout on `zbus_sub_wait()` solely to periodically wake up and drain a private `k_msgq` (`disc_msgq`) that carries discovery events. This dual-input polling pattern exists because discovery events were kept off zbus — zbus channels are broadcast-overwrite, so rapid successive announces (e.g., temp + humidity from the same BLE node) would collapse into a single event, losing data.

In practice, discovery events arrive rarely enough that latest-wins is acceptable. The dedicated thread and 50ms polling timeout are unnecessary complexity.

## Solution

Move discovery events onto a new zbus channel, eliminate the private `k_msgq`, eliminate the dedicated thread entirely, and replace it with two zbus listeners that dispatch to work items on a small dedicated workqueue.

## Architecture

### New zbus channel: `remote_discovery_chan`

```
ZBUS_CHAN_DEFINE(remote_discovery_chan, struct remote_discovery_event)
```

Defined in `lib/remote_sensor/src/remote_discovery_chan.c`, declared in the public header. Follows ADR-002: one `.c` file per channel, `ZBUS_CHAN_DECLARE` in the header.

### `remote_sensor_announce_disc()` becomes a thin wrapper

Transports call the same API. Internally it publishes to the zbus channel instead of enqueueing to `k_msgq`:

```c
int remote_sensor_announce_disc(const struct remote_discovery_event *evt)
{
    return zbus_chan_pub(&remote_discovery_chan, evt, K_MSEC(100));
}
```

Same return semantics (0 on success, negative errno on failure). Same 100ms timeout for queue-full backpressure.

### Listener + workqueue dispatch

Replace the dedicated thread (`K_THREAD_DEFINE`) with two zbus listeners and a small workqueue:

```c
static K_WORK_QUEUE_DEFINE(remote_sensor_wq, 512,
                           CONFIG_REMOTE_SENSOR_THREAD_PRIORITY, 0);

static struct k_work disc_work;
static struct k_work scan_work;
static struct remote_discovery_event pending_disc;
static struct remote_scan_ctrl_event pending_scan;
static K_MUTEX_DEFINE(pending_mutex);
```

**Discovery listener:**

```c
static void disc_work_handler(struct k_work *work)
{
    struct remote_discovery_event evt;

    k_mutex_lock(&pending_mutex, K_FOREVER);
    evt = pending_disc;
    k_mutex_unlock(&pending_mutex);
    handle_discovery(&evt);
}

static void discovery_cb(const struct zbus_channel *chan)
{
    const struct remote_discovery_event *evt = zbus_chan_const_msg(chan);

    k_mutex_lock(&pending_mutex, K_FOREVER);
    pending_disc = *evt;
    k_mutex_unlock(&pending_mutex);
    k_work_submit_to_queue(&remote_sensor_wq, &disc_work);
}

ZBUS_LISTENER_DEFINE(remote_disc_listener, discovery_cb);
```

**Scan control listener:**

```c
static void scan_work_handler(struct k_work *work)
{
    struct remote_scan_ctrl_event evt;

    k_mutex_lock(&pending_mutex, K_FOREVER);
    evt = pending_scan;
    k_mutex_unlock(&pending_mutex);
    // existing scan dispatch logic (STRUCT_SECTION_FOREACH)
}

static void scan_ctrl_cb(const struct zbus_channel *chan)
{
    const struct remote_scan_ctrl_event *evt = zbus_chan_const_msg(chan);

    k_mutex_lock(&pending_mutex, K_FOREVER);
    pending_scan = *evt;
    k_mutex_unlock(&pending_mutex);
    k_work_submit_to_queue(&remote_sensor_wq, &scan_work);
}

ZBUS_LISTENER_DEFINE(remote_scan_ctrl_listener, scan_ctrl_cb);
```

**SYS_INIT:**

```c
static int remote_sensor_manager_init(void)
{
    k_work_init(&disc_work, disc_work_handler);
    k_work_init(&scan_work, scan_work_handler);
    // No zbus_chan_add_obs needed — ZBUS_LISTENER_DEFINE auto-registers

#if defined(CONFIG_REMOTE_SENSOR_AUTO_SCAN)
    struct remote_scan_ctrl_event scan_all = {
        .action = REMOTE_SCAN_START,
        .proto = REMOTE_TRANSPORT_PROTO_UNKNOWN,
    };
    zbus_chan_pub(&remote_scan_ctrl_chan, &scan_all, K_MSEC(100));
    LOG_INF("auto-scan started");
#endif

    LOG_INF("remote_sensor_manager: init done");
    return 0;
}
```

### Key properties

- **No dedicated thread.** Saves stack memory (was `CONFIG_REMOTE_SENSOR_THREAD_STACK_SIZE`) and scheduler overhead.
- **Listeners run in publisher context.** Must not block — they only copy to a static buffer and submit a work item.
- **Mutex protects pending buffers.** Two listeners share one mutex; contention is negligible since events are rare.
- **Latest-wins is inherent.** If a second discovery event arrives before the first work item runs, the pending buffer is overwritten. This is acceptable per the design constraint.
- **Workqueue stack is small.** 512 bytes is sufficient — `handle_discovery()` does registry lookups, mutex locks, and settings writes, but no large stack allocations.

### Confirm + retry (future, transport-layer)

For wireless transports that need delivery guarantees, the confirm pattern lives in the transport adapter, not the manager:

1. Transport publishes discovery on zbus and starts a retry timer (e.g., 500ms)
2. Manager, after `handle_discovery()`, publishes an ack on a new `remote_discovery_ack_chan`
3. Transport's ack listener cancels the retry timer
4. If timer fires before ack, transport re-publishes

This is deferred. `pipe_transport` and `fake_remote_sensor` need no retry — they have their own "seen" tracking. Only real wireless transports (BLE, LoRa) will need this when implemented.

## Impact

### Files changed

| File | Change |
|---|---|
| `lib/remote_sensor/src/remote_discovery_chan.c` | **New** — `ZBUS_CHAN_DEFINE` for `remote_discovery_chan` |
| `lib/remote_sensor/src/remote_sensor_manager.c` | Remove `disc_msgq`, remove `K_THREAD_DEFINE`, remove `ZBUS_SUBSCRIBER_DEFINE`, remove `zbus_sub_wait` loop, add two `ZBUS_LISTENER_DEFINE` + work items + workqueue, update `SYS_INIT` |
| `lib/remote_sensor/include/remote_sensor/remote_sensor.h` | Add `ZBUS_CHAN_DECLARE(remote_discovery_chan)`, update `remote_sensor_announce_disc` docstring, update header doc about discovery delivery |
| `lib/remote_sensor/CMakeLists.txt` | Add `remote_discovery_chan.c` to sources |
| `docs/architecture/diagrams/zbus-channels.mmd` | Add `remote_discovery_chan` to channel map |
| `docs/architecture/event-bus.md` | Update discovery delivery description |

### Files unchanged

| File | Reason |
|---|---|
| `lib/pipe_transport/src/pipe_transport.c` | Calls `remote_sensor_announce_disc()` — API preserved |
| `lib/fake_remote_sensor/src/fake_remote_sensor.c` | Calls `remote_sensor_announce_disc()` — API preserved |
| `handle_discovery()` logic | Auto-register, persist, mark-lost all unchanged |
| `remote_scan_ctrl_chan` | Unchanged |
| `remote_trigger_listener` | Unchanged — still a listener, still routes triggers |

## Risks

1. **Pending buffer overwrite:** If two discovery events arrive before the first work item runs, the second overwrites the pending buffer. Mitigation: acceptable per "latest-wins" constraint. If this becomes a problem, switch to a small static ring buffer instead of a single-slot buffer.

2. **Workqueue stack overflow:** `handle_discovery()` calls `register_peer()` which locks a mutex, copies strings, and calls `sensor_registry_set_meta()`. 512 bytes should be sufficient, but should be verified at runtime with `CONFIG_THREAD_STACK_INFO=y`.

3. **EEXIST epoll collision:** Adding a new zbus channel does not add a new socket — zbus uses in-process messaging, not network sockets. No epoll risk.

## Testing

- Existing integration tests should pass unchanged (API preserved)
- `fake_remote_sensor` scan + auto-register flow exercises the new channel
- `pipe_transport` discovery exercises the new channel
- Shell `remote_sensor scan start` exercises scan_ctrl listener alongside discovery listener
