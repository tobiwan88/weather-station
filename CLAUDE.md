# CLAUDE.md — weather-station

Project context for AI coding agents. Read this before writing any code.
Full rationale is in [`docs/adr/`](docs/adr/README.md).

---

## Identity

| | |
|---|---|
| **RTOS** | Zephyr v4.2.0 |
| **Topology** | West T2 — repo is both manifest and module |
| **Primary target** | `native_sim` (no hardware dependency in v1) |
| **Container** | `executed form devcontainer, prepares the setup` |
---

## Essential commands

```bash

west init -l .                       # first-time workspace setup
west update --narrow                 # fetch Zephyr + allowlist modules

west build -p always -b native_sim apps/gateway
west build -p always -b native_sim apps/sensor-node
west build -t run                    # run the last built app

west twister -p native_sim -T tests/ --inline-logs -v -N
pre-commit run --all-files
```

---

## Architecture rules (non-negotiable)

**1. One event = one physical measurement.**
Temperature and humidity from the same chip are two separate `env_sensor_data`
events on `sensor_event_chan`. Never bundle multiple measurements.

**2. `env_sensor_data` is a flat 20-byte struct — no pointers.**
```c
struct env_sensor_data {
    uint32_t         sensor_uid;    // DT sensor-uid property
    enum sensor_type type;          // what physical quantity
    int32_t          q31_value;     // Q31 fixed-point value
    int64_t          timestamp_ms;  // k_uptime_get()
};                                  // sizeof == 20 — enforced by BUILD_ASSERT
```

**3. No sensor manager. No polling. No tight coupling.**
Sensors subscribe to `sensor_trigger_chan`. Any publisher fires them.
See [ADR-004](docs/adr/ADR-004-trigger-driven-sampling.md).

**4. `main.c` contains only:** `LOG_MODULE_REGISTER` + optional `SYS_INIT` +
`k_sleep(K_FOREVER)`. All logic lives in libraries.
See [ADR-008](docs/adr/ADR-008-kconfig-app-composition.md).

**5. Fake sensors are production-quality drivers**, not stubs.
Instantiated via `DT_FOREACH_STATUS_OKAY`. Shell-controllable.
See [ADR-005](docs/adr/ADR-005-fake-sensor-subsystem.md).

**6. `sensor_uid` is the identity key.** `sensor_registry` maps uid → metadata.
Display/MQTT use the registry — never hardcode UIDs in consumers.

**7. zbus channel ownership is strict.**
`ZBUS_CHAN_DEFINE` in exactly one `.c` per channel.
`ZBUS_CHAN_DECLARE` in the public header only.
See [ADR-002](docs/adr/ADR-002-zbus-as-system-bus.md).
trigger_chan → fake_sensor → sensor_event_chan → (consumers)

**8. Apps configure features via Kconfig only.**
No `target_link_libraries()` in app `CMakeLists.txt`.
See [ADR-001](docs/adr/ADR-001-repo-and-workspace-structure.md).

---

## Agent workflow (non-negotiable)

Follow this workflow for **every** task that touches source code.

### 1. Branch first
Before writing any code, create a feature branch from `main`:
```bash
git checkout main && git pull
git checkout -b <short-kebab-description>   # e.g. feat/sensor-uid-logging
```
Never commit directly to `main` or `master`.

### 2. Incremental changes + build gate
Make the smallest possible logical change, then verify it compiles:
```bash
west build -p always -b native_sim apps/gateway
west build -p always -b native_sim apps/sensor-node
```
If the build fails, fix it before touching anything else.

### 3. Test gate (when source code is touched)
Run the full test suite after every non-trivial change:
```bash
west twister -p native_sim -T tests/ --inline-logs -v -N
```
All tests must be green before committing. Never commit a red suite.

### 4. Commit per logical unit
Once build and tests are green, run pre-commit and commit:
```bash
pre-commit run --all-files
git add <changed files>
git commit -m "<type>(<scope>): <imperative summary>"
```
Commit message conventions:
- **type**: `feat` | `fix` | `refactor` | `test` | `docs` | `chore`
- **scope**: library or app name, e.g. `fake_sensors`, `gateway`
- **summary**: imperative, ≤72 chars, no period

Repeat steps 2–4 for each logical unit of work.

### 5. Hand off to user for merge
When all work on the branch is done, tell the user:
> "Branch `<name>` is ready. All builds pass and tests are green.
> Run `git merge --no-ff <name>` or open a PR to merge into main."

Do **not** merge, push, or open a PR yourself unless explicitly asked.

---
## What NOT to create

- A `sensor_manager` module of any kind
- Any polling loop calling `sensor_sample_fetch()`
- `CONFIG_BME280`, `CONFIG_SHT4X`, or any real sensor driver
- Wi-Fi, MQTT, HTTP, or display code (not in scope yet)
- Files under `.west/` or `build/`
