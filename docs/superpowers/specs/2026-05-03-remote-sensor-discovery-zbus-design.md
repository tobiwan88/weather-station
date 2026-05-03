# Design: Move Remote Sensor Discovery to zbus

**Date:** 2026-05-03
**Status:** Approved
**Author:** opencode

## Problem

The `remote_sensor_manager` thread uses a 50ms timeout on `zbus_sub_wait()` solely to periodically wake up and drain a private `k_msgq` (`disc_msgq`) that carries discovery events. This dual-input polling pattern exists because discovery events were kept off zbus — zbus channels are broadcast-overwrite, so rapid successive announces (e.g., temp + humidity from the same BLE node) would collapse into a single event, losing data.

In practice, discovery events arrive rarely enough that latest-wins is acceptable. The 50ms polling timeout is unnecessary complexity.

## Solution

Move discovery events onto a new zbus channel, eliminate the private `k_msgq`, and replace `zbus_sub_wait(K_MSEC(50))` with `zbus_sub_wait(K_FOREVER)`.

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

### Manager thread loop

Before:
```c
while (true) {
    while (k_msgq_get(&disc_msgq, &disc_evt, K_NO_WAIT) == 0) {
        handle_discovery(&disc_evt);
    }
    rc = zbus_sub_wait(&remote_sensor_sub, &chan, K_MSEC(50));
    if (rc == -EAGAIN) { continue; }
    // ... handle scan_ctrl ...
}
```

After:
```c
while (true) {
    zbus_sub_wait(&remote_sensor_sub, &chan, K_FOREVER);

    if (chan == &remote_discovery_chan) {
        struct remote_discovery_event evt;
        zbus_chan_read(&remote_discovery_chan, &evt, K_NO_WAIT);
        handle_discovery(&evt);
    } else if (chan == &remote_scan_ctrl_chan) {
        // existing scan dispatch logic (unchanged)
    }
}
```

No timeout. No drain loop. No `disc_msgq`. The subscriber queue itself provides FIFO ordering — if multiple discovery events arrive while processing one, they queue up and are delivered in order. If the queue overflows (depth 8), zbus naturally delivers the latest, which is acceptable for this workload.

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
| `lib/remote_sensor/src/remote_sensor_manager.c` | Remove `disc_msgq`, remove drain loop, add `remote_discovery_chan` to subscriber, change `K_MSEC(50)` → `K_FOREVER`, add `remote_discovery_chan` observer in `SYS_INIT` |
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
| Trigger listener | Unchanged |

### Subscriber queue depth

The existing `ZBUS_SUBSCRIBER_DEFINE(remote_sensor_sub, 8)` has depth 8. This must hold both `remote_scan_ctrl_chan` and `remote_discovery_chan` events. With discovery being rare and scan events being user-initiated, 8 is sufficient. If needed later, increase to 16.

## Risks

1. **Queue overflow during burst discovery:** If `fake_remote_sensor` announces 4 sensor types × N nodes rapidly, the subscriber queue (depth 8) could overflow. Mitigation: zbus delivers latest on overflow, which is acceptable per the "latest-wins" constraint. If this becomes a problem, increase subscriber depth.

2. **EEXIST epoll collision:** Adding a new zbus channel does not add a new socket — zbus uses in-process messaging, not network sockets. No epoll risk.

## Testing

- Existing integration tests should pass unchanged (API preserved)
- `fake_remote_sensor` scan + auto-register flow exercises the new channel
- `pipe_transport` discovery exercises the new channel
- Shell `remote_sensor scan start` exercises scan_ctrl alongside discovery on the same subscriber
