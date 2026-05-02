# Documentation GitHub Pages Design

## Context

Weather station currently has benchmark data publishing to GitHub Pages (`dev/bench/`). Project documentation (ADR, architecture docs) lives in `docs/` but is not published. The goal is to publish docs to GitHub Pages and create a root index page linking to both benchmark and docs.

## Decision

Publish MkDocs-built documentation alongside benchmark data on the same `gh-pages` branch.

## Architecture

```
gh-pages branch
├── index.html          ← root entry page (static, committed)
├── dev/
│   ├── bench/          ← benchmark data (existing)
│   └── docs/           ← MkDocs site
```

## Workflow Changes

### File: `.github/workflows/benchmark.yml`

Add parallel `docs` job:

| Job | Trigger | Action |
|-----|---------|--------|
| `footprint` | push/PR to main | Unchanged |
| `docs` | `docs/**` changed OR workflow_dispatch | `mkdocs build` → deploy to `dev/docs/` |

### Jobs run in parallel (no dependency between them)

## New Files

| File | Purpose |
|------|---------|
| `mkdocs.yml` | MkDocs config with Material theme, source from `docs/` |
| `docs/index.md` | Welcome page (optional, MkDocs handles navigation) |
| `index.html` | **Root entry page** at repo root — links to benchmark and docs |

### Root `index.html`

Static file committed to repo. Updated manually when needed.

```html
<!DOCTYPE html>
<html>
<head>
  <title>Weather Station</title>
</head>
<body>
  <h1>Weather Station</h1>
  <ul>
    <li><a href="dev/bench/">Benchmark</a></li>
    <li><a href="dev/docs/">Documentation</a></li>
  </ul>
</body>
</html>
```

## Build Dependencies

Docs job needs:
- Python with `mkdocs` + `mkdocs-material` packages

## Deployment Flow (docs job)

1. Checkout `gh-pages` branch
2. Install `mkdocs` + `mkdocs-material`
3. Run `mkdocs build` (outputs to `site/`)
4. Copy `site/*` → `dev/docs/` in gh-pages working tree
5. Ensure `index.html` exists at gh-pages root
6. Commit and push

## Testing

- Verify `dev/docs/` accessible at `<org>.github.io/<repo>/dev/docs/`
- Verify root index links work
- Verify benchmark link still works at `dev/bench/`
