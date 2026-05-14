# Diagram Conventions Guide

Use this guide when adding or updating architecture diagrams for the weather-station project.

## When to use each diagram type

| Type | Use for |
|------|---------|
| `graph TB` / `graph LR` | Component relationships, dependency graphs, layered architecture views |
| `sequenceDiagram` | Message flows, time-ordered interactions, init sequences, request-response cycles |
| `flowchart` | Decision trees, algorithm flows — use sparingly, prefer sequenceDiagram for flows with actors |

Do not use `graph TD` for sequential flows — use `sequenceDiagram` instead.

## Project colour palette

All existing diagrams use this palette. Match it exactly for new diagrams:

```
Application layer:  fill:#dbeafe, stroke:#3b82f6  (blue)
Library layer:      fill:#dcfce7, stroke:#16a34a  (green)
Channel nodes:      fill:#fef9c3, stroke:#ca8a04  (yellow)
Consumer nodes:     fill:#fce7f3, stroke:#db2777  (pink)
Zephyr RTOS nodes:  fill:#fce7f3, stroke:#db2777  (pink)
Transport nodes:    fill:#e0e7ff, stroke:#6366f1  (indigo)
```

Apply with `style <ID> fill:#..., stroke:#...` at the bottom of graph diagrams.

## File naming and location

- **Source file:** `docs/architecture/diagrams/<kebab-name>.mmd`
- **Content:** raw Mermaid source only — no markdown code fences
- **Frontmatter:** add a title block at the top:
  ```
  ---
  title: Human-readable title
  ---
  ```
- **Inline embed** (in any arch page or diagrams.md):
  ````markdown
  ```mermaid
  --8<-- "<kebab-name>.mmd"
  ```
  ````

## Where to add a new diagram

1. **Create** `docs/architecture/diagrams/<kebab-name>.mmd`
2. **Add to catalog** `docs/architecture/diagrams.md` — append a new `## Section` with a one-line description and the snippet include
3. **Embed inline** in the most relevant arch page, at the natural point in the narrative where the diagram adds meaning the text doesn't already provide

Only embed where a visual genuinely clarifies structure or flow. Do not add diagrams to every section.

## Style rules

- **Subgraph labels:** ALL-CAPS SHORT name + `\n` detail line, e.g. `["LIBRARY LAYER (lib/)\nself-wired via SYS_INIT"]`
- **Edge labels:** lowercase imperative, e.g. `-->|publishes|`, `-->|subscribes|`, `-->|Kconfig selects|`
- **Sequence participants:** use `as` aliases for multi-line names, e.g. `participant DASH as http_dashboard<br/>(ring buffer)`
- **Sequence notes:** `Note over X: brief context` — one short phrase, not a paragraph
- **Line breaks in labels:** use `<br/>` inside node labels and participant aliases

## What NOT to do

- Do not embed long inline Mermaid blocks directly in arch pages — always use a `.mmd` file + snippet include
- Do not use `graph TD` for sequential flows (use `sequenceDiagram`)
- Do not add diagrams to a page just because a diagram exists — only embed where it adds meaning
- Do not use colours outside the project palette without a strong reason
- Do not commit `.mmd` files with markdown code fences around the Mermaid source
