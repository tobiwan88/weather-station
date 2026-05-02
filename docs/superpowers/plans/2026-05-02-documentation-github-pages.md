# Documentation GitHub Pages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish project documentation to GitHub Pages alongside existing benchmark data, with a root index page linking to both.

**Architecture:** Add parallel `docs` job to existing benchmark workflow that builds MkDocs and deploys to `dev/docs/` on the `gh-pages` branch. Root `index.html` stays committed to repo.

**Tech Stack:** MkDocs + Material theme, GitHub Actions

---

## File Structure

```
.github/workflows/benchmark.yml  → modify (add docs job)
mkdocs.yml                       → create
index.html                       → create (root entry page)
docs/index.md                    → create (welcome page)
```

---

## Task 1: Create `mkdocs.yml`

**File:** `mkdocs.yml` (at repo root)

- [ ] **Step 1: Write mkdocs.yml configuration**

```yaml
site_name: Weather Station Documentation
site_url: https://tobiwan88.github.io/weather-station/
repo_url: https://github.com/tobiwan88/weather-station
repo_name: tobiwan88/weather-station

theme:
  name: material
  palette:
    - scheme: default
      primary: indigo
      accent: indigo
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - scheme: slate
      primary: indigo
      accent: indigo
      toggle:
        icon: material/brightness-4
        name: Switch to light mode
  features:
    - navigation.tabs
    - navigation.sections
    - search.suggest
    - search.highlight

nav:
  - Home: index.md
  - Architecture:
    - Overview: architecture/index.md
    - System Overview: architecture/system-overview.md
    - Event Bus: architecture/event-bus.md
    - Composition Model: architecture/composition-model.md
    - Integration Tests: architecture/integration-tests.md
    - Concurrency: architecture/concurrency.md
  - ADR:
    - adr/index.md
  - Backlog: backlog.md
```

- [ ] **Step 2: Commit**

```bash
git add mkdocs.yml
git commit -m "docs: add mkdocs.yml configuration"
```

---

## Task 2: Create `docs/index.md`

**File:** `docs/index.md` (at repo root)

- [ ] **Step 1: Write welcome page**

```markdown
# Weather Station

Welcome to the Weather Station project documentation.

## Project Structure

This project uses Zephyr RTOS on a network of sensors and a central gateway.

## Quick Links

- [Architecture](architecture/index.md)
- [Architecture Decision Records](adr/index.md)
- [Backlog](backlog.md)
```

- [ ] **Step 2: Commit**

```bash
git add docs/index.md
git commit -m "docs: add welcome page for MkDocs"
```

---

## Task 3: Create root `index.html`

**File:** `index.html` (at repo root)

- [ ] **Step 1: Write root index page**

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Weather Station</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; max-width: 600px; margin: 50px auto; padding: 0 20px; }
    h1 { color: #333; }
    ul { list-style: none; padding: 0; }
    li { margin: 15px 0; }
    a { color: #0066cc; text-decoration: none; font-size: 18px; }
    a:hover { text-decoration: underline; }
  </style>
</head>
<body>
  <h1>Weather Station</h1>
  <ul>
    <li><a href="dev/bench/">Benchmark</a> — ROM/RAM footprint trends</li>
    <li><a href="dev/docs/">Documentation</a> — Architecture, ADRs, and project docs</li>
  </ul>
</body>
</html>
```

- [ ] **Step 2: Commit**

```bash
git add index.html
git commit -m "docs: add root index.html for GitHub Pages"
```

---

## Task 4: Update `.github/workflows/benchmark.yml`

**File:** `.github/workflows/benchmark.yml`

- [ ] **Step 1: Add workflow_dispatch to triggers**

Old:
```yaml
on:
  push:
    branches: [main, master]
  pull_request:
    branches: [main, master]
```

New:
```yaml
on:
  push:
    branches: [main, master]
  pull_request:
    branches: [main, master]
  workflow_dispatch:
```

- [ ] **Step 2: Add docs job after footprint job (parallel, using paths-filter)**

After the `footprint` job closing brace (after line 112), add:

```yaml
  docs:
    name: Build and Deploy Docs
    runs-on: ubuntu-latest
    steps:
      - id: changes
        uses: dorny/paths-filter@v2
        with:
          filters: |
            docs:
              - 'docs/**'
              - 'mkdocs.yml'
              - 'index.html'

      - if: steps.changes.outputs.docs == 'true'
        name: Checkout gh-pages
        uses: actions/checkout@v4
        with:
          repository: ${{ github.repository }}
          ref: gh-pages
          token: ${{ secrets.GITHUB_TOKEN }}
          fetch-depth: 0

      - if: steps.changes.outputs.docs == 'true'
        name: Configure git user
        run: |
          git config --global user.email "ci@example.com"
          git config --global user.name "CI Bot"

      - if: steps.changes.outputs.docs == 'true'
        name: Install MkDocs and Material theme
        run: pip install mkdocs mkdocs-material

      - if: steps.changes.outputs.docs == 'true'
        name: Build MkDocs site
        run: mkdocs build

      - if: steps.changes.outputs.docs == 'true'
        name: Deploy to GitHub Pages
        run: |
          cp -r site/* .
          cp index.html .
          git add -A
          git diff --staged --exit-code || git commit -m "docs: update GitHub Pages $(date +'%Y-%m-%d %H:%M:%S')"
          git push
```

Note: The `dorny/paths-filter` step always runs (even on PRs that don't touch docs), but subsequent steps use `if: steps.changes.outputs.docs == 'true'` so they only execute when docs files changed. This means the job shows as "success" even when nothing changed — that's expected.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/benchmark.yml
git commit -m "docs: add mkdocs build and deploy to GitHub Pages"
```

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/benchmark.yml
git commit -m "docs: add mkdocs build and deploy to GitHub Pages"
```

- [ ] **Step 2: Update workflow triggers**

Old:
```yaml
on:
  push:
    branches: [main, master]
  pull_request:
    branches: [main, master]
```

New:
```yaml
on:
  push:
    branches: [main, master]
  pull_request:
    branches: [main, master]
  workflow_dispatch:
```

Note: footprint job runs on every push (it's fast and only runs on main). Docs job uses paths-filter to determine if it needs to actually build.

- [ ] **Step 3: Add docs job after footprint job (parallel, using paths-filter)**

After the `footprint` job closing brace, add:

```yaml
  docs:
    name: Build and Deploy Docs
    runs-on: ubuntu-latest
    steps:
      - id: changes
        uses: dorny/paths-filter@v2
        with:
          filters: |
            docs:
              - 'docs/**'
              - 'mkdocs.yml'
              - 'index.html'

      - if: steps.changes.outputs.docs == 'true'
        name: Checkout gh-pages
        uses: actions/checkout@v4
        with:
          repository: ${{ github.repository }}
          ref: gh-pages
          token: ${{ secrets.GITHUB_TOKEN }}
          fetch-depth: 0

      - if: steps.changes.outputs.docs == 'true'
        name: Configure git user
        run: |
          git config --global user.email "ci@example.com"
          git config --global user.name "CI Bot"

      - if: steps.changes.outputs.docs == 'true'
        name: Install MkDocs and Material theme
        run: pip install mkdocs mkdocs-material

      - if: steps.changes.outputs.docs == 'true'
        name: Build MkDocs site
        run: mkdocs build

      - if: steps.changes.outputs.docs == 'true'
        name: Deploy to GitHub Pages
        run: |
          cp -r site/* .
          cp index.html .
          git add -A
          git diff --staged --exit-code || git commit -m "docs: update GitHub Pages $(date +'%Y-%m-%d %H:%M:%S')"
          git push

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/benchmark.yml
git commit -m "docs: add mkdocs build and deploy to GitHub Pages"
```

---

## Verification

1. **Local MkDocs build test:**
   ```bash
   pip install mkdocs mkdocs-material
   mkdocs build
   # Verify site/ directory contains built HTML
   ```

2. **Review workflow syntax** using `actionlint` or GitHub's workflow validation

3. **Verify paths filter** correctly triggers on `docs/**` changes

---

## Post-Implementation

After merging:
1. Confirm `dev/docs/` accessible at `https://tobiwan88.github.io/weather-station/dev/docs/`
2. Verify root `index.html` links work
3. Confirm benchmark still accessible at `dev/bench/`
