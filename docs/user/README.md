# User Guide

This guide covers how to interact with the Weather Station system as an operator.

## Interfaces

The Weather Station exposes three user interfaces:

| Interface | Purpose | Access |
|---|---|---|
| **HTTP Dashboard** | Live sensor data, charts, runtime configuration | Browser on port 8080 |
| **UART Shell** | Ad-hoc inspection, sensor control, debugging | Serial console |
| **LVGL Display** | Analog clock + live sensor cards | SDL window (native_sim) |

## HTTP Dashboard

The web dashboard is the primary interface for monitoring and configuration.

### Accessing the Dashboard

1. Open a browser and navigate to `http://<device-ip>:8080`
2. On `native_sim` running locally: `http://localhost:8080`

### Pages

| Page | URL | Description |
|---|---|---|
| **Dashboard** | `/` | Live Chart.js timeseries of recent sensor readings, auto-refreshes every second |
| **Configuration** | `/config` | HTML form for runtime settings |
| **Login** | `/login` | Authentication page (only when `CONFIG_HTTP_DASHBOARD_AUTH=y`) |

### Configuration via API

The dashboard exposes a REST API for programmatic access:

| Endpoint | Method | Description |
|---|---|---|
| `/api/data` | GET | JSON snapshot of recent sensor readings |
| `/api/config` | GET / POST | Read or update runtime configuration |
| `/api/login` | POST | Authenticate and receive a session cookie |
| `/api/logout` | POST | Invalidate current session |
| `/api/change-credentials` | POST | Change username and password |
| `/api/locations` | GET | List registered locations |
| `/api/token/rotate` | POST | Rotate the API bearer token |

Configurable settings include:
- Sensor trigger interval
- SNTP server and manual resync
- MQTT broker settings (server, port, authentication, gateway ID)
- Sensor metadata (display name, location, user data)
- Location management (add/remove/list)

### Authentication

When `CONFIG_HTTP_DASHBOARD_AUTH=y`:
- Browser users log in via `/login` with username/password
- API clients send `Authorization: Bearer <token>` header
- Default credentials are set via Kconfig at build time
- Credentials are persisted in Zephyr settings (`dash/user`, `dash/pass`, `dash/token`)
  - First login with default credentials forces a credential change (`POST /api/change-credentials`); other APIs return 403 `password_change_required` until then.
  - After 5 consecutive failed logins `/api/login` returns 429 with `Retry-After`.
  - `GET /api/config` does not return the API token; use the `http_dashboard token show` shell command.

When `CONFIG_HTTP_DASHBOARD_AUTH=n` (default for development), all endpoints are open.

## UART Shell

The serial shell provides command-line access to subsystems. Connect via the serial console of your target device.

### Available Commands

| Command | Description |
|---|---|
| `fake_sensors list` | List all fake sensor instances |
| `fake_sensors temperature_set <uid_hex> <mdegC>` | Set a fake sensor's temperature |
| `fake_sensors humidity_set <uid_hex> <m_pct_rh>` | Set a fake sensor's humidity |
| `fake_sensors co2_set <uid_hex> <mppm>` | Set a fake sensor's CO2 value |
| `fake_sensors voc_set <uid_hex> <mIAQ>` | Set a fake sensor's VOC index |
| `fake_sensors trigger [uid_hex]` | Manually trigger a sensor sample (omit for broadcast) |
| `mqtt_pub status` | Show MQTT publisher status |
| `mqtt_pub set <key> <value>` | Update MQTT settings |
| `location add <name>` | Register a new location |
| `location remove <name>` | Remove a location |
| `location list` | List all registered locations |
| `remote_sensor list` | List paired remote sensors |
| `remote_sensor scan start\|stop [proto]` | Start or stop a remote sensor scan |
| `lora provision <id>` | Generate a pre-shared key for a node (gateway) |
| `lora forget <id>` | Remove a node session and its key (gateway) |
| `lora sessions` | List sessions without keys (gateway) |
| `lora_node provision <id> <32-hex-key>` | Store the node's pre-shared key (node) |
| `lora_node status` | Show node id, provisioning state and next seq (node) |
| `http_dashboard token show\|rotate` | Print or rotate the API bearer token |

## LVGL Display

When `CONFIG_LVGL_DISPLAY=y` and a display backend is available (SDL on `native_sim`), a 320×240 window shows:
- Analog clock (synced via SNTP)
- Sensor data cards with live readings

The display is read-only — configuration must be done via the HTTP dashboard or shell.

## Kconfig Features

Features are enabled at build time via Kconfig. Key options:

| Option | Default | Description |
|---|---|---|
| `CONFIG_HTTP_DASHBOARD` | y | Web dashboard on port 8080 |
| `CONFIG_HTTP_DASHBOARD_AUTH` | y (gateway), n (Kconfig) | Require login for dashboard access |
| `CONFIG_LVGL_DISPLAY` | y | SDL display window |
| `CONFIG_MQTT_PUBLISHER` | y | MQTT sensor data publishing |
| `CONFIG_FAKE_SENSORS` | y | Fake sensor drivers (native_sim) |
| `CONFIG_FAKE_SENSORS_AUTO_PUBLISH_MS` | 1000 | Auto-publish interval for fake sensors |
| `CONFIG_NET_CONFIG_CLOCK_SNTP_INIT` | y | Boot-time SNTP sync (Zephyr net_config) |
| `CONFIG_PIPE_PUBLISHER` | n | POSIX pipe output (integration testing) |
