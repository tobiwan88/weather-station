# Renode and Trace Development Workflow

This guide covers the end-to-end development loop: building the gateway for
Renode, running it in the emulator, capturing a trace dump, and analyzing
thread-level performance in Perfetto UI.

---

## Prerequisites

- Devcontainer or bare-metal Linux host
- Renode installed (`./.devcontainer/install-renode.sh` in the devcontainer)
- Python 3 (already in the devcontainer venv)

---

## One-command workflow

The `scripts/trace-and-analyze` script performs the full pipeline:

```bash
./scripts/trace-and-analyze
```

This does three things:
1. Builds the gateway with trace recorder enabled for the `frdm_mcxn947` target
2. Runs the `gateway_trace.robot` Renode test (boots gateway, waits for shell, dumps trace)
3. Runs `parse-trace.py` to produce a summary and a Perfetto-compatible trace file

Output files land in `simulation/renode/`:
- `trace.bin` — raw binary trace buffer (32 KB)
- `trace_functions.perfetto` — Renode function profiler output
- `trace_combined.json` — unified Perfetto Chrome Tracing JSON

Open `trace_combined.json` in [Perfetto UI](https://ui.perfetto.dev) by
dragging and dropping the file, or by serving it from a local HTTP server.

---

## Manual step-by-step workflow

### 1. Build with trace support

```bash
ZEPHYR_BASE=/home/zephyr/workspace/zephyr west build -p auto \
  -b frdm_mcxn947/mcxn947/cpu0 --sysbuild \
  --build-dir build/renode-trace apps/gateway -- \
  -DDTC_OVERLAY_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.overlay" \
  -DEXTRA_CONF_FILE="boards/frdm_mcxn947_mcxn947_cpu0_renode.conf;boards/frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf"
```

The trace recorder Kconfig fragment enables:
- `CONFIG_TRACING=y` + `CONFIG_TRACING_USER=y`
- `CONFIG_TRACE_RECORDER=y` with 4096-record buffer
- `CONFIG_TRACE_RECORDER_SHELL=y` for shell interaction

### 2. Run in Renode

```bash
renode-test simulation/renode/tests/gateway_trace.robot
```

This boots the gateway, waits for the Zephyr shell prompt, lets it run for
5 seconds to accumulate trace data, then runs the `Dump Trace` keyword which
flushes the profiler and reads the trace buffer via `sysbus ReadMemory`.

### 3. Parse the trace

```bash
python3 scripts/parse-trace.py \
  --trace simulation/renode/trace.bin \
  --elf build/renode-trace/gateway/zephyr/zephyr.elf \
  --profiler simulation/renode/trace_functions.perfetto \
  --output simulation/renode/trace_combined.json \
  --summary
```

The summary prints to stdout:
```
Trace summary:
  Total records:    8423
  Context switches: 2105
  ISR entries:      38
  Tracked threads:  12

Thread               Runtime %  Switches     ID
----------------------------------------------------
http_dashboard          34.2%        456      7
mqtt_publisher          22.1%        312      8
sntp_sync_thread         8.5%         89      3
...
```

### 4. View in Perfetto

Open [ui.perfetto.dev](https://ui.perfetto.dev), drag `trace_combined.json` into
the browser, and inspect:
- **Thread timeline** — when each thread ran, for how long
- **Context switches** — scheduling events between threads
- **Function flame graphs** — what functions consumed CPU within each thread

---

## Renode platform files

### `.resc` script

`simulation/renode/frdm_mcxn947.resc` is the Renode start-up script. Key macros:

| Macro | Purpose |
|-------|---------|
| `reset` | Load ELF, set vector table, enable Zephyr mode, start profiler + function logging |
| `dump-trace` | Flush profiler, read `trace_records` from memory, write to `trace.bin` |

### `.repl` platform description

`simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl` describes the MCXN947
peripherals: CPU (Cortex-M33), RAM, flash, UART, GPIO, timers.

### Kconfig / DTS overrides

Renode requires hardware-specific workarounds because it lacks functional
models for Ethernet (ENET-QoS) and external SPI NOR flash:

| File | Purpose |
|------|---------|
| `frdm_mcxn947_mcxn947_cpu0_renode.conf` | Disable ENET, FlexSPI NOR, FOTA confirm |
| `frdm_mcxn947_mcxn947_cpu0_renode.overlay` | Remove ENET and external flash DT nodes |
| `frdm_mcxn947_mcxn947_cpu0_trace_recorder.conf` | Enable trace recorder |

These are temporary — they will be removed when Renode gains functional
ENET-QoS and SPI NOR peripheral models (see backlog `[RENODE-ENET-QOS]`
and `[RENODE-SPI-NOR]`).

---

## Robot Framework test structure

Tests live in `simulation/renode/tests/`:

```
simulation/renode/tests/
├── common.robot          ← Suite Setup/Teardown, shared keywords
├── gateway_boot.robot    ← Smoke: boot, shell, stability
├── gateway_fota.robot    ← FOTA: MCUboot swap, confirm, rollback
└── gateway_trace.robot   ← Trace: collect trace dump after boot
```

### Common keywords

| Keyword | Purpose |
|---------|---------|
| `Prepare Machine ${elf}` | Load platform, ELF, set vector table, start emulation |
| `Wait For Shell Prompt` | Block until `uart:~$` appears |
| `Wait For Boot Banner` | Block until `*** Booting Zephyr OS` appears |
| `Send Shell Command ${cmd}` | Send a command and wait for echo |
| `Dump Trace` | Flush profiler, read `trace_records` from memory |

### Adding a new test

1. Create `simulation/renode/tests/<name>.robot`
2. `Resource common.robot` at the top
3. Use `Prepare Machine` + `Wait For Shell Prompt` to boot
4. Use `Execute Command` for Renode commands, `Write Line To Uart` for shell
5. Assert with Robot Framework built-ins (`Should Not Be Equal`, etc.)

---

## CI integration

The `renode` CI job builds with trace config and publishes artifacts:

| Artifact | Retention |
|----------|-----------|
| `trace.bin` | 7 days |
| `trace_functions.perfetto` | 7 days |
| `zephyr.elf` | 7 days |

The `deploy-results` job deploys parsed traces (Perfetto JSON) to GitHub Pages
under `traces/<ref>/trace_combined.json` for deep-linking from Perfetto UI.
See [ci-dev-environment.md](ci-dev-environment.md) for the full CI pipeline.

---

## Known limitations

- **No live streaming** — trace data is only available post-mortem. For
  interactive debugging, use the `trace dump` shell command.
- **Bounded buffer** — 32 KB default captures ~4 seconds at 1000 ctx/sec.
  Increase `CONFIG_TRACE_RECORDER_BUFFER_SIZE` for longer coverage at the
  cost of BSS memory.
- **No function tracing on firmware** — function-level call stacks come
  from Renode's profiler, not from firmware instrumentation. Renode function
  logging uses symbol names with a prefix filter (`k_ z_ work_ mqtt_ net_
  zbus_ mbedtls_`).
- **Renode peripheral gaps** — ENET-QoS and SPI NOR require workaround
  Kconfig/DTS overrides (see backlog).
