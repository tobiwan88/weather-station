# Concurrency

## Execution Contexts

Five distinct contexts run concurrently in the gateway:

| Context | What runs there | Scheduling |
|---|---|---|
| Timer ISR | `fake_sensors_timer` expiry — publishes to trigger channel | Hardware timer, preempts everything |
| zbus thread | All zbus listener callbacks | Zephyr internal thread, runs after each publish |
| System work queue | `k_work_delayable` handlers (sntp resync, clock display) | Cooperative within the work queue thread |
| HTTP server thread(s) | HTTP request handlers | Per Zephyr HTTP server configuration |
| Main thread | LVGL render loop or `k_sleep(K_FOREVER)` | Lowest priority |

zbus listeners are dispatched sequentially from the zbus thread in registration order. A listener that blocks stalls all subsequent listeners on that channel. Listeners must not sleep or acquire mutexes that another thread might hold — only spinlocks are safe.

---

## Synchronisation Choices

### `k_spinlock` in `http_dashboard`

The dashboard's ring buffer is written by the sensor_event zbus listener and read by HTTP request handlers. The zbus listener is ultimately triggered by a timer ISR (timer → trigger channel publish → sensor driver → event channel publish → dashboard listener), which means the listener runs in a context that could preempt the HTTP handler thread.

`k_mutex` cannot be used here: acquiring a mutex from ISR-derived context is undefined behaviour in Zephyr. `k_spinlock` is ISR-safe on both sides.

The lock scope is kept to the minimum: acquire, `memcpy` the ring buffer snapshot, release. JSON serialisation happens outside the lock. This keeps the critical section short regardless of how many sensors are registered or how large the history buffer is.

### `k_mutex` in `sensor_registry`

The registry is written by sensor driver `SYS_INIT` callbacks (priority 90/91) and read by the HTTP dashboard handler. Neither of these runs from ISR context. A mutex is correct here and preferable: if `CONFIG_SENSOR_REGISTRY_SETTINGS=y`, saving user metadata calls into Zephyr's settings subsystem which may block on flash I/O. A spinlock cannot be held across a blocking call.

### `atomic_t` in `sntp_sync`

The synced flag is written once (after the first successful NTP query) and read frequently (by anything that calls `sntp_sync_is_ready()`). An atomic variable is sufficient — no locking overhead, no ISR restriction.

---

## The Snapshot Pattern

The HTTP dashboard demonstrates the recommended pattern for sharing data between an interrupt-derived producer and a thread-based consumer:

```
producer (zbus listener, ISR-derived)     consumer (HTTP handler thread)
─────────────────────────────────         ──────────────────────────────
k_spinlock_lock(&lock, &key)
  append sample to ring buffer
k_spinlock_unlock(&lock, &key)
                                          k_spinlock_lock(&lock, &key)
                                            memcpy ring_buf → snapshot
                                          k_spinlock_unlock(&lock, &key)
                                          serialize snapshot → JSON
                                          write HTTP response
```

The snapshot step eliminates the need to hold the spinlock during serialisation. This is important because JSON serialisation is O(n) in the number of samples, and holding a spinlock for a variable and potentially long duration would block the ISR from appending new samples.

---

## Initialisation Ordering and Race Conditions

The SYS_INIT priority ordering prevents two specific races:

**Trigger fired before listener registered.** The sensors timer starts at priority 99. All sensor driver listeners register at priority 90/91. If the timer started earlier, the first broadcast would be lost. zbus delivers to registered listeners only at the moment of publish — there is no replay buffer.

**Dashboard starts before registry is populated.** The HTTP dashboard initialises at priority 97. Sensor drivers register in the registry at priority 90/91. If these were reversed, the first HTTP request might see an empty sensor list even though sensors are active.

The startup trigger (fired at priority 90/91 by each sensor driver's `SYS_INIT`) uses a different path: it publishes to `sensor_trigger_chan`, which dispatches back to the sensor driver's own listener synchronously in the zbus thread. The gateway listener (priority 95) and dashboard listener (priority 97) are not yet registered at that point, so they miss the startup sample — this is intentional. The startup trigger exists only to prime the sensor's internal state, not to produce a visible reading.
