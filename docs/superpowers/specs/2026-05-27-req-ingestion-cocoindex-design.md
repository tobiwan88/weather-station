# DESIGN — Requirements Ingestion & CocoIndex Zephyr Corpus

| Field | Value |
|-------|-------|
| **Status** | Approved |
| **Date** | 2026-05-27 |
| **Version** | 0.1 |

---

## Overview

Two complementary workstreams:
1. **CocoIndex Layer 2** — Zephyr corpus indexer for semantic search over Zephyr's public API, samples, subsystems, DTS bindings, and board files
2. **REQ file ingestion** — structured requirements documents seeded from existing ADRs and architecture docs

Constrained by ADR-008 (Kconfig-only composition), ADR-009 (native_sim first), ADR-010 (CI/dev env).

---

## 1. CocoIndex Layer 2

### Script: `scripts/cocoindex_zephyr_flow.py`

Uses `cocoindex` Python SDK to define flows. 7 scopes:

| Flow name | Path | Patterns |
|-----------|------|----------|
| `includes` | `ZEPHYR/include/zephyr/` | `*.h` |
| `samples` | `ZEPHYR/samples/` | `*.c`, `*.h`, `CMakeLists.txt`, `prj.conf`, `*.overlay`, `*.dts`, `*.conf`, `Kconfig*`, `*.yaml` |
| `subsys` | `ZEPHYR/subsys/` | `*.h`, `Kconfig*` |
| `dts_bindings` | `ZEPHYR/dts/bindings/` | `*.yaml` |
| `drivers` | `ZEPHYR/drivers/` | `include/**/*.h`, `Kconfig*` |
| `boards_mcxn947` | `ZEPHYR/boards/` | `frdm_mcxn947*/**/*.dts`, `*.h`, `Kconfig*`, `nxp/mcxn947*/**` |
| `hal_nxp` | `modules/hal/nxp/` | `*.h`, `Kconfig*` |

`ZEPHYR_BASE` auto-detected from env or defaults to `/home/zephyr/workspace/zephyr`.

### MCP Config Updates

**`opencode.json`:** Add `cocoindex-zephyr` MCP server alongside existing `cocoindex-code`.
**`.mcp.json`:** Same addition.

### Verification

- `python scripts/cocoindex_zephyr_flow.py update` — runs index
- `ccc index` — re-indexes project code
- Search queries verify quality

---

## 2. REQ File Ingestion

### Directory: `docs/requirements/`

### Template: `TEMPLATE.md`

Standard structure: Status, Version, Context, Functional requirement, Dependencies, Constraints, Acceptance criteria (tagged [native_sim]/[renode]/[hil]/[manual]), Related ADRs, Related Zephyr subsystems.

### 11 Domain Files (one per domain)

| File | Scope |
|------|-------|
| `REQ-SENSORS.md` | Temp, humidity, pressure, CO2, VOC, PM2.5/PM10, gas resistance, Q31, trigger-driven, fake sensors |
| `REQ-LORA.md` | Packet format, provisioning, RPC retry, FOTA window, session persistence, Ed25519 |
| `REQ-MQTT.md` | Broker auth, topic hierarchy, QoS, runtime reconfig, self-heal reconnect |
| `REQ-HTTP.md` | Dashboard port 8080, Chart.js, REST API, session auth, FOTA upload |
| `REQ-DISPLAY.md` | LVGL 320x240 SDL, analog clock, sensor cards, event subscription, ADR-008 |
| `REQ-FOTA.md` | MCUboot dual-image, signed updates, health-check confirm, HTTP transport, recovery |
| `REQ-POWER.md` | Sleep states, wake sources, PM integration, outdoor duty cycle |
| `REQ-TIME.md` | SNTP sync, resync on config, timestamp contract, pre-sync delay |
| `REQ-CONFIG.md` | config_cmd channel, supported commands, settings persistence, shell parity |
| `REQ-LOCATION.md` | Named location CRUD, settings persistence, uid-independent metadata |
| `REQ-DATA.md` | zbus ownership, event bus contract, sensor event struct, protobuf wire, ring buffer |

Each populated from all 16 ADRs + architecture docs with complete requirements, constraints, acceptance criteria, and cross-references.

---

## 3. Commit Plan

1. `feat(scripts): add CocoIndex Zephyr corpus indexer flow script`
2. `feat(config): add cocoindex-zephyr MCP server config`
3. `feat(docs): add REQ template and 11 domain requirement files`
4. `docs(arch): update PLAN status to implemented`

---

## 4. Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| CocoIndex SDK API differs from plan | Probe installed version, adapt |
| REQ file population is content-heavy | Batch-read ADRs in parallel |
| Zephyr upstream churn | Smoke check for file count drop >20% |
| REQ staleness | Cross-reference ADRs in `## Related ADRs` |
