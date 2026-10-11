---
name: new-diagram
description: Use when adding or updating an architecture diagram, Mermaid chart or sequence diagram in docs/architecture/diagrams/ and its catalog entry. Triggers: "diagram", "mermaid", "sequence diagram", "flow chart".
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Add or Update an Architecture Diagram

Diagrams are raw Mermaid sources in `docs/architecture/diagrams/`. Each is embedded in an
architecture page and listed in `docs/architecture/diagrams.md`. No generator script exists:
the `.mmd` file is the source of truth and pages include it with a snippet.

## Choose the diagram type

| Type | Use for |
|------|---------|
| `graph TB` / `graph LR` | Component relationships, dependency graphs, layered views |
| `sequenceDiagram` | Message flows, init sequences, request-response, anything time-ordered |
| `flowchart` | Decision trees and algorithms. Use sparingly |

Do not use `graph TD` for sequential flows. Use `sequenceDiagram`.

## File format

- File: `docs/architecture/diagrams/<kebab-name>.mmd`
- Start with a front-matter title block, as the existing files do:
  ```
  ---
  title: Human-readable title
  ---
  ```
- Raw Mermaid only. No markdown code fences inside the `.mmd` file.

## Project palette

Apply with `style <ID> fill:#..., stroke:#...` at the bottom of graph diagrams. Use no other colours.

```
Application layer:  fill:#dbeafe, stroke:#3b82f6  (blue)
Library layer:      fill:#dcfce7, stroke:#16a34a  (green)
Channel nodes:      fill:#fef9c3, stroke:#ca8a04  (yellow)
Consumer / Zephyr:  fill:#fce7f3, stroke:#db2777  (pink)
Transport nodes:    fill:#e0e7ff, stroke:#6366f1  (indigo)
```

## Style rules

- Subgraph labels: ALL-CAPS name plus a `\n` detail line. Edge labels: lowercase imperative
  (`-->|publishes|`). Line breaks in labels: `<br/>`.
- Sequence participants: `as` aliases, e.g. `participant DASH as http_dashboard<br/>(ring buffer)`.
- Sequence notes: `Note over X: brief phrase`, not a paragraph.

## Where it goes

1. Create `docs/architecture/diagrams/<kebab-name>.mmd`.
2. Append a `## Section` to `docs/architecture/diagrams.md` with a one-line description and the snippet:
   ````markdown
   ```mermaid
   --8<-- "<kebab-name>.mmd"
   ```
   ````
3. Embed the same snippet inline in the most relevant page under `docs/architecture/`, at the
   point where the diagram adds meaning the text does not. Do not add diagrams to every section,
   and do not paste long Mermaid blocks into pages.

## Verification

Diagram-only changes need no firmware build. Render the source to check it parses:

```bash
npx --yes @mermaid-js/mermaid-cli mmdc -i docs/architecture/diagrams/<kebab-name>.mmd -o "${TMPDIR:-/tmp}/<kebab-name>.svg"
```

If `mmdc` is unavailable, check the source at https://mermaid.live. Do not proceed with a
diagram that fails to render. Then run `pre-commit run --all-files` before committing. Commit as `docs(arch): add <kebab-name> diagram`.
