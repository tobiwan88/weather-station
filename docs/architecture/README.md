# Architecture

These documents cover the system architecture. Read in order for a complete picture, or jump to a topic below.

| Document | What it explains |
|---|---|
| [architecture-constraints.md](architecture-constraints.md) | Auto-generated constraint summary from all ADRs — read this first |
| [system-overview.md](system-overview.md) | Goals, layers, library roles, and design rules |
| [event-bus.md](event-bus.md) | Why two channels, the trigger–event split, ISR safety, and message design |
| [composition-model.md](composition-model.md) | How Kconfig + SYS_INIT eliminates explicit wiring in `main.c` |
| [concurrency.md](concurrency.md) | Execution contexts, why spinlock vs mutex, the snapshot pattern, init ordering |
| [integration-tests.md](integration-tests.md) | Pytest integration tests: harnesses, markers, data flow, session rules, HIL path |
| [firmware-update.md](firmware-update.md) | MCUboot FOTA: flash layout, sysbuild, MCUmgr transports, HTTP upload, confirm/rollback |
| [trace-recorder.md](trace-recorder.md) | Thread context-switch trace recorder: Zephyr tracing hooks, ring buffer, Renode dump |
| [renode-trace-workflow.md](renode-trace-workflow.md) | Developer workflow: build → Renode → trace dump → parse → Perfetto UI |
| [lora-protocol.md](lora-protocol.md) | LoRa wireless protocol: frame format, provisioning, FOTA relay, RPC, compact data |
| [fake-sensors.md](fake-sensors.md) | Fake sensor subsystem: module structure, DT bindings, shell commands, data flow |
| [native-sim.md](native-sim.md) | native_sim development target: what works, MQTT/LVGL/LoRa setup, CI integration |
| [ci-dev-environment.md](ci-dev-environment.md) | Container image, devcontainer workflow, CI pipeline, code quality tools |
| [http-dashboard.md](http-dashboard.md) | HTTP endpoints, ring-buffer snapshot, linker fragment, authentication |
| [diagrams.md](diagrams.md) | All architecture diagrams rendered inline |
