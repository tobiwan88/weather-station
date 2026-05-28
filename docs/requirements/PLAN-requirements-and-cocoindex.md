# PLAN — Requirements Ingestion & CocoIndex Corpus Setup

| Field | Value |
|-------|-------|
| **Status** | Implemented |
| **Date** | 2026-05-24 |
| **Version** | 1.0 |

---

## Overview

This plan covers two complementary workstreams:

1. **CocoIndex Layer 2** — a Zephyr corpus indexer giving agents semantic search over Zephyr's public API, samples, subsystems, DTS bindings, and board files.
2. **REQ file ingestion** — structured requirements documents for the weather-station codebase, seeded from existing ADRs and architecture docs.

---

## 1. State Before

### Layer 1 (project code) — Done

- `ccc` CLI at `/home/zephyr/.local/bin/ccc`, Python `cocoindex` 1.0.6
- `.cocoindex_code/settings.yml` covers all file types
- `opencode.json` + `.mcp.json` have `cocoindex-code` MCP (`ccc mcp`)
- Index: 1797 chunks across 269 files (markdown, C, Python, C++, YAML, etc.)

### Layer 2 (Zephyr corpus) — Missing

No Zephyr-side index exists. Agent searches are limited to the project tree.

### REQ files — Missing

No `docs/requirements/` directory. Requirements exist implicitly in ADRs, architecture docs, and the backlog.

---

## 2. CocoIndex Layer 2 — Zephyr Corpus Indexer

**File:** `scripts/cocoindex_zephyr_flow.py`

Scopes (future-proof — all Zephyr features indexed):

| Flow name | Path | Patterns | Notes |
|-----------|------|----------|-------|
| `includes` | `ZEPHYR/include/zephyr/` | `*.h` | All public API headers, recursive |
| `samples` | `ZEPHYR/samples/` | `*.c`, `*.h`, `CMakeLists.txt`, `prj.conf`, `*.overlay`, `*.dts`, `*.conf`, `Kconfig*`, `*.yaml` | All samples for extensibility |
| `subsys` | `ZEPHYR/subsys/` | `*.h`, `Kconfig*` | Subsystem headers and Kconfig |
| `dts_bindings` | `ZEPHYR/dts/bindings/` | `*.yaml` | All DTS bindings |
| `drivers` | `ZEPHYR/drivers/` | `include/**/*.h`, `Kconfig*`, `*/Kconfig` | Driver headers and Kconfig |
| `boards_mcxn947` | `ZEPHYR/boards/` | `frdm_mcxn947*/**/*.dts`, `frdm_mcxn947*/**/*.h`, `frdm_mcxn947*/**/Kconfig*`, `nxp/mcxn947*/**` | Main hardware target |
| `hal_nxp` | `modules/hal/nxp/` | `*.h`, `Kconfig*` | NXP MCXN947 HAL |

`ZEPHYR_BASE` auto-detected from env or defaults to `/home/zephyr/workspace/zephyr`.

### MCP config updates

**`opencode.json`:**
```json
{
  "mcp": {
    "cocoindex-code": { "command": ["ccc", "mcp"] },
    "cocoindex-zephyr": {
      "command": ["python", "scripts/cocoindex_zephyr_flow.py", "mcp"]
    }
  }
}
```

**`.mcp.json`:**
```json
{
  "mcpServers": {
    "cocoindex-code": { "command": "ccc", "args": ["mcp"] },
    "cocoindex-zephyr": {
      "command": "python",
      "args": ["scripts/cocoindex_zephyr_flow.py", "mcp"]
    }
  }
}
```

### Initial run

```bash
cd /home/zephyr/workspace/weather-station
python scripts/cocoindex_zephyr_flow.py update   # ~5-10 min first run
ccc index
```

### Verification queries

- "How does Zephyr sensor driver API work" → surfaces `include/zephyr/drivers/sensor.h`
- "MCUboot FOTA confirm" → surfaces `subsys/dfu/` + board overlay files
- "Zephyr MQTT publish example" → surfaces `samples/net/mqtt_publisher/`
- "FRDM-MCXN947 DTS" → surfaces MCXN947 board files

---

## 3. REQ File Ingestion

### Directory

`docs/requirements/`

### Template

```markdown
# REQ-DOMAIN-NNN — Title

## Status
draft | review | approved | deprecated

## Version
0.1

## Context
Why this feature/constraint exists.

## Functional requirement
The system shall [verb] [object] [condition].

## Dependencies
- REQ-DOMAIN-NNN: [reason]
- (or "none yet")

## Constraints
- Power: ≤ [X] µA average during [mode]
- Latency: ≤ [X] ms [from event to output]
- Memory: ≤ [X] KB [SRAM/flash]

## Acceptance criteria
Each criterion tagged with how it can be verified:

- [ ] [native_sim] ztest: [description]
- [ ] [renode] Integration: [description]
- [ ] [hil] Power test: [description]
- [ ] [manual] Board: [description]

## Related ADRs
- ADR-NNNN (if exists, else "none yet")

## Related Zephyr subsystems
- CONFIG_*, etc.
```

### Domains

| REQ File | Scope |
|----------|-------|
| `REQ-SENSORS-*.md` | Temp, humidity, pressure, CO2, VOC, PM2.5/PM10, gas resistance. Q31 encode/decode, trigger-driven sampling, BME680 forced-mode timing, SEN0460 settle time, fake sensor DT instantiation |
| `REQ-LORA-*.md` | Packet format (magic + CRC8), provisioning handshake, RPC retry, FOTA window protocol, session persistence, Ed25519 key caching |
| `REQ-MQTT-*.md` | Broker auth, topic hierarchy, QoS, runtime reconfiguration, self-heal reconnect |
| `REQ-HTTP-*.md` | Dashboard on port 8080, Chart.js timeseries, REST API, session auth + bearer token, FOTA upload endpoint |
| `REQ-DISPLAY-*.md` | LVGL 320x240 SDL window, analog clock, sensor cards, sensor event subscription, ADR-008 compliance |
| `REQ-FOTA-*.md` | MCUboot dual-image, signed updates, health-check confirm, HTTP transport, manual recovery path, key secrecy in CI |
| `REQ-POWER-*.md` | Sleep states, wake sources, BME680/SEN0460 PM integration, outdoor sensor node duty cycle |
| `REQ-TIME-*.md` | SNTP sync at boot + periodic, resync on config command, timestamp contract (ms granularity), native_sim pre-sync delay |
| `REQ-CONFIG-*.md` | `config_cmd` channel contract, supported commands, settings persistence, shell interface parity |
| `REQ-LOCATION-*.md` | Named location CRUD, settings persistence, uid-independent metadata |
| `REQ-DATA-*.md` | zbus channel ownership, event bus contract, sensor event struct (flat, 20-24B), protobuf wire format, ring buffer snapshot pattern |

### Acceptance criteria tagging

Every criterion receives one of: `[native_sim]`, `[renode]`, `[hil]`, `[manual]`.

This prevents CI from reporting green while leaving the hardest constraints (power, timing, real hardware) silently unverified. The test runner in Phase 2+ uses these tags to produce a "deferred verification" report separately from the test pass/fail count.

---

## 4. Execution Order

1. Create `scripts/cocoindex_zephyr_flow.py` → index → verify search quality
2. Create REQ template file: `docs/requirements/TEMPLATE.md`
3. Create REQ files per domain (one session per domain with Claude/OpenCode)
4. Re-index after REQ ingestion: `ccc index && python scripts/cocoindex_zephyr_flow.py update`
5. Add `ccc index` + flow update to pre-commit or a cron/alias for periodic refresh

---

## 5. Risks

- **CocoIndex dependency:** If `ccc` breaks, agents lose search. Fallback: raw `rg` on project + Zephyr trees.
- **Zephyr upstream churn:** Board/DTS paths can change between releases, silently dropping index coverage. Mitigation: add a smoke check that warns if indexed file count drops >20%.
- **REQ staleness:** REQ files can drift from implementation. Mitigation: cross-reference ADRs and `## Related ADRs` field keeps them traceable.
