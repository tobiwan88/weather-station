# Architecture

Four documents cover the system architecture. Read them in order for a complete picture.

| Document | What it explains |
|---|---|
| [system-overview.md](system-overview.md) | Goals, layers, library roles, and design rules |
| [event-bus.md](event-bus.md) | Why two channels, the trigger–event split, ISR safety, and message design |
| [composition-model.md](composition-model.md) | How Kconfig + SYS_INIT eliminates explicit wiring in `main.c` |
| [concurrency.md](concurrency.md) | Execution contexts, why spinlock vs mutex, the snapshot pattern, init ordering |
| [integration-tests.md](integration-tests.md) | Pytest integration tests: harnesses, markers, data flow, session rules, HIL path |
| [diagrams.md](diagrams.md) | All architecture diagrams rendered inline |
