# Architecture

Four documents cover the system architecture. Read them in order for a complete picture.

| Document | What it explains |
|---|---|
| [system-overview.md](system-overview.md) | Goals, layers, library roles, and design rules |
| [event-bus.md](event-bus.md) | Why two channels, the trigger–event split, ISR safety, and message design |
| [composition-model.md](composition-model.md) | How Kconfig + SYS_INIT eliminates explicit wiring in `main.c` |
| [concurrency.md](concurrency.md) | Execution contexts, why spinlock vs mutex, the snapshot pattern, init ordering |

## Diagrams

Mermaid sources are in [`diagrams/`](diagrams/). To render:

```bash
./generate-diagrams.sh          # SVG via mmdc or Docker; HTML preview if neither available
./generate-diagrams.sh --html   # always render browser-viewable HTML
./generate-diagrams.sh --check  # print which backend will be used
```

| Diagram | What it shows |
|---|---|
| [system-overview.mmd](diagrams/system-overview.mmd) | Layered component view |
| [zbus-channels.mmd](diagrams/zbus-channels.mmd) | Publishers, channels, and subscribers |
| [data-flow.mmd](diagrams/data-flow.mmd) | Sensor trigger → event → consumers sequence |
| [library-deps.mmd](diagrams/library-deps.mmd) | Inter-library dependency graph |
| [init-sequence.mmd](diagrams/init-sequence.mmd) | SYS_INIT boot ordering |
| [http-flow.mmd](diagrams/http-flow.mmd) | POST /api/config side-effects |
