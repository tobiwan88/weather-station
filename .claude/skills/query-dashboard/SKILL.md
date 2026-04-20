---
name: query-dashboard
description: Query or update the HTTP dashboard on port 8080. Accepts one argument: data | config | set-interval | resync.
argument-hint: <action>
disable-model-invocation: false
allowed-tools: Bash
---

# Query Dashboard

**Argument received**: action=`$0`

## Actions

### `data` — read sensor ring-buffer

```bash
curl -s http://localhost:8080/api/data | python3 -m json.tool
```

Returns JSON array of recent sensor readings (`sensor_uid`, `type`, `q31_value`, `timestamp_ms`).

### `config` — read current runtime config

```bash
curl -s http://localhost:8080/api/config | python3 -m json.tool
```

Returns JSON with `trigger_interval_ms` and `sntp_server`.

### `set-interval` — change trigger interval

```bash
curl -s -X POST http://localhost:8080/api/config \
  -d "trigger_interval_ms=2000"
```

Replace `2000` with the desired interval in milliseconds. Takes effect immediately. Verify with the `config` action.

### `resync` — trigger SNTP resync

```bash
curl -s -X POST http://localhost:8080/api/config \
  -d "action=sntp_resync"
```

Submits a `k_work_delayable` via `sntp_sync_trigger_resync()`. Check the Zephyr log for `sntp_sync:` lines to confirm.

## Prerequisites

Gateway must be running with `CONFIG_HTTP_DASHBOARD=y`. Dashboard self-initialises at `SYS_INIT` APPLICATION priority 97. Default port: `HTTP_DASHBOARD_PORT=8080`.

## Do NOT

- Do not POST to `GET /` or `GET /config` — those are HTML pages, not API endpoints.
- Do not use `PUT` or `DELETE` — only `GET` and `POST` are handled.
