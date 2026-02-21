# weather-station

An open-source IoT weather station built on [Zephyr RTOS v4.2.0](https://zephyrproject.org/).
All sensor logic is decoupled through **zbus** channels — no polling loops, no central manager.

Container image: [ghcr.io/tobiwan88/zephyr_docker](https://github.com/tobiwan88/zephyr_docker)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        gateway (native_sim)                     │
│                                                                 │
│  ┌──────────────┐   sensor_trigger_chan   ┌──────────────────┐  │
│  │ auto-publish │ ─────────────────────► │ fake_temperature │  │
│  │   timer      │                        │ fake_humidity    │  │
│  └──────────────┘                        └────────┬─────────┘  │
│                                                   │             │
│                              sensor_event_chan    ▼             │
│  ┌──────────────┐ ◄──────────────────────────────────────────  │
│  │ gateway log  │   {uid, type, q31_value, timestamp_ms}        │
│  │ (zbus sub)   │                                               │
│  └──────────────┘                                               │
│                                                                 │
│  sensor_registry: uid → {label, location, is_remote}           │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     sensor-node (native_sim)                    │
│                                                                 │
│  sensor_trigger_chan ──► fake_temperature (uid=0x0021)          │
│                      ──► fake_humidity    (uid=0x0022)          │
│                                │                                │
│                    sensor_event_chan ──► node log               │
│                                         (future: LoRa TX)       │
└─────────────────────────────────────────────────────────────────┘

Key design rules
────────────────
• One event = one physical measurement (temperature and humidity are
  separate env_sensor_data events, even from the same chip)
• Sensors subscribe to sensor_trigger_chan — any publisher can fire them
• sensor_uid is the identity key; consumers use sensor_registry, never
  hardcode UIDs
• main.c contains only: LOG_MODULE_REGISTER + SYS_INIT + k_sleep(K_FOREVER)
```

---

## Quick Start

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/) or Podman
- [VS Code](https://code.visualstudio.com/) + Dev Containers extension (optional)

### 1 — Open in dev container (recommended)

```bash
git clone https://github.com/<your-org>/weather-station
code weather-station
# VS Code: "Reopen in Container"
# onCreateCommand runs: west init -l . && west update --narrow
```

### 2 — Manual (inside the container)

```bash
# Start the container
docker run -it --rm \
  -v "$(pwd)/weather-station:/home/zephyr/workspace/weather-station" \
  -w /home/zephyr/workspace/weather-station \
  ghcr.io/tobiwan88/zephyr_docker:latest

# Inside container
source ~/.venv/bin/activate

# Initialise west workspace (first time only)
west init -l .
west update --narrow

# Build gateway
west build -p always -b native_sim apps/gateway

# Run gateway (Ctrl+C to stop)
west build -t run
```

### 3 — Build sensor-node

```bash
west build -p always -b native_sim apps/sensor-node
west build -t run
```

### 4 — Run tests with Twister

```bash
west twister -p native_sim -T tests/ --inline-logs -v -N
```

---

## Shell Commands

Once the gateway is running (native_sim opens a UART shell on stdin/stdout):

```
# List all registered fake sensors
uart:~$ fake_sensors list
UID     Kind          Location              Current value
------  ------------  --------------------  ----------------
0x0001  temperature   living_room           21000 mdeg C
0x0002  humidity      living_room           50000 m%RH
0x0011  temperature   outdoor               4000 mdeg C
0x0012  humidity      outdoor               30000 m%RH

# Set indoor temperature to 23.5 °C (= 23500 mdeg C)
uart:~$ fake_sensors temperature_set 0x0001 23500

# Set outdoor humidity to 72.5 %RH (= 72500 m%RH)
uart:~$ fake_sensors humidity_set 0x0012 72500

# Fire a manual broadcast trigger
uart:~$ fake_sensors trigger

# Fire a trigger targeting a specific sensor
uart:~$ fake_sensors trigger 0x0001
```

---

## Project Structure

```
weather-station/
├── west.yml                    # T2 manifest — imports Zephyr v4.2.0
├── CMakeLists.txt              # Module-level (no find_package(Zephyr))
├── Kconfig                     # rsource lib/Kconfig
├── zephyr/module.yml           # Declares this repo as a Zephyr module
├── apps/
│   ├── gateway/                # Full gateway app (auto-publish, shell, log)
│   └── sensor-node/            # Lightweight sensor node app
├── lib/
│   ├── sensor_event/           # env_sensor_data struct + sensor_event_chan
│   ├── sensor_trigger/         # sensor_trigger_event + sensor_trigger_chan
│   ├── sensor_registry/        # uid → {label, location} map
│   └── fake_sensors/           # DT-instantiated fake temp/humidity drivers
├── include/common/
│   └── weather_messages.h      # Re-exports sensor_event.h + sensor_trigger.h
├── dts/bindings/               # DT bindings: fake,temperature + fake,humidity
├── tests/
│   ├── sensor_event/           # Q31 round-trip + sizeof tests
│   └── fake_sensors/           # Trigger → event integration test
└── .devcontainer/              # VS Code dev container configuration
```

---

## Data Format

All measurements travel as `env_sensor_data` on `sensor_event_chan`:

```c
struct env_sensor_data {
    uint32_t         sensor_uid;    /* DT-assigned unique ID   (4 bytes) */
    enum sensor_type type;          /* physical quantity       (4 bytes) */
    int32_t          q31_value;     /* Q31 fixed-point value   (4 bytes) */
    int64_t          timestamp_ms;  /* k_uptime_get()          (8 bytes) */
};                               /* total: 20 bytes — LoRa-friendly    */
```

**Q31 encoding ranges:**

| Type        | Physical range  | Q31 = 0           | Q31 = INT32_MAX  |
|-------------|-----------------|-------------------|-----------------|
| Temperature | -40 .. +85 °C   | -40.0 °C          | +85.0 °C        |
| Humidity    | 0 .. 100 %RH    | 0.0 %RH           | 100.0 %RH       |

---

## License

Apache-2.0 — see [LICENSE](LICENSE).
