# CocoIndex Code Integration — Design

**Date:** 2026-05-15
**Status:** approved

## Goal

Integrate CocoIndex Code (`ccc`) as a semantic code search layer for OpenCode. Reduce tool-call overhead (fewer `grep`/`read` round-trips) and improve cross-file context awareness when the agent works in this codebase.

## Architecture

```
OpenCode ──MCP── ccc mcp (daemon) ── Vector Index (LanceDB on disk) ── /home/zephyr/workspace/
```

- **MCP server:** `ccc mcp` exposes a `search(query, limit, languages, paths)` tool
- **Indexing:** `ccc index` uses Tree-sitter AST-aware chunking for C, C++, Python, H, Shell, Markdown, YAML
- **Embeddings:** Local via `sentence-transformers` (`Snowflake/snowflake-arctic-embed-xs`, ~334MB model)
- **Incremental:** Only changed files re-chunked and re-embedded on subsequent `ccc index` runs

## Indexing Scope

**Root:** `/home/zephyr/workspace/` — covers weather-station + Zephyr + all west modules.

### Indexed (~15K files)

| Scope | Files | Rationale |
|-------|-------|-----------|
| `weather-station/{lib,app,tests,docs,scripts}` | 232 | All own code |
| Zephyr core (`kernel,include,subsys,drivers,arch,lib,soc,cmake`) | 9,444 | Kernel APIs used by weather-station |
| Zephyr DTS bindings (`dts/`) | 2,504 | DT overlay context for boards |
| `modules/lib/gui/lvgl` | 1,811 | Used by `lvgl_display` |
| `modules/crypto/mbedtls` | 137 | Used by `mqtt_publisher` for TLS |
| `modules/lib/loramac-node` | 1,159 | Used by LoRa connectivity |
| `modules/lib/nanopb` | 151 | Used by protobuf serialization |

### Excluded (~18K files, 55% reduction)

| Scope | Files | Rationale |
|-------|-------|-----------|
| `modules/hal/*` (Nordic, NXP, CMSIS) | 13,496 | Vendor HALs, never directly referenced |
| `bootloader/mcuboot` | 336 | FOTA bootloader, not relevant for native_sim |
| `modules/debug/segger` | 17 | Debug probe support |
| `zephyr/tests` | 2,697 | Zephyr's own kernel tests |
| `zephyr/samples` | 1,011 | Example code |
| `zephyr/boards` | 231 | Board config (only native_sim + mcxn947 needed) |
| `build/`, `.west/`, `twister-out*/` | — | Build/west artifacts |

## Configuration

### Project settings (`.cocoindex_code/settings.yml`)

```yaml
exclude_patterns:
  - "**/build/**"
  - "**/.west/**"
  - "**/twister-out*/**"
  - "**/bootloader/**"
  - "**/modules/hal/**"
  - "**/modules/debug/**"
  - "**/zephyr/tests/**"
  - "**/zephyr/samples/**"
  - "**/zephyr/boards/**"
```

### Global settings (`~/.cocoindex_code/global_settings.yml`)

```yaml
embedding:
  provider: sentence-transformers
  model: Snowflake/snowflake-arctic-embed-xs
```

## Resource Budget

| Resource | Estimate | Available |
|----------|----------|-----------|
| Index DB (LanceDB) on disk | ~800MB–1.5GB | 87GB free |
| Model + index in RAM | ~1.5GB | 4.8GB free |
| Initial index time | ~5-8 min | One-time |
| Incremental re-index | <1s per save | — |

## Workflow

1. **Install:** `pip install pipx && pipx install 'cocoindex-code[full]'`
2. **Init:** `ccc init --root /home/zephyr/workspace` → generates `settings.yml`
3. **Configure exclusions:** Edit `.cocoindex_code/settings.yml` with exclude patterns above
4. **Index:** `ccc index` — one-time, ~5-8 min
5. **Start MCP:** `ccc mcp` — starts the daemon, OpenCode auto-discovers it
6. **Re-index:** `ccc index` after pulling new Zephyr changes or adding modules

## Effect on Agent Behavior

**Before:** Agent reads CLAUDE.md → greps for pattern → reads 3-5 files → discovers cross-references → reads 2-3 more files. ~6-8 tool calls per exploration.

**After:** Agent issues one `ccc_search("sensor trigger event publishing")` → gets ranked AST chunks from `sensor_trigger.c`, `fake_sensor.c`, `mqtt_publisher.c`, `config_cmd.c`. ~1 tool call.

**Example queries the agent can use:**
- "where are zbus channels defined"
- "how does sensor_event_chan get published"
- "MQTT topic construction logic"
- "Kconfig composition for fake_sensors"

## Non-Goals

- No Dockerfile or devcontainer changes (manual setup, no devcontainer used)
- No auto-start daemon (user starts `ccc mcp` manually when desired)
- No CI integration
- No team-shared index (single-developer use)
