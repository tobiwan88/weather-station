# ADR-008 — Kconfig-Only App Composition

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Zephyr applications can be structured in many ways. Libraries can be linked
explicitly via `target_link_libraries()` in CMake, selected via Kconfig, or
discovered automatically. The choice has a direct impact on:

- How easy it is to port an app to a new board
- How easy it is for an AI agent to generate new features without creating
  CMake coupling mistakes
- Whether swapping a sensor driver (BME280 → SHT4x) requires source changes

The project goal is that **each app's `src/main.c` is under 50 lines** and
each app's `prj.conf` is the **single file that defines what the app does**.
A developer — or AI agent — reading `prj.conf` should be able to understand
the full feature set of the firmware image without reading CMake files.

---

## Decision

Apps compose features **exclusively via Kconfig**. CMakeLists.txt files for
apps contain only the minimal Zephyr boilerplate. All feature selection,
library inclusion, and driver activation is done in `prj.conf` and board
`.conf` files.

For file templates (app CMakeLists.txt, library CMakeLists.txt, full prj.conf example,
board .conf overlay, Kconfig dependency chain), and the "when app-level C code is
acceptable" table, see [`docs/architecture/composition-model.md`](../architecture/composition-model.md).

---

## Consequences

**Easier:**
- A developer (or AI agent) can understand the entire feature set of a firmware
  image by reading one file: `prj.conf`.
- Porting to new hardware = write a board overlay `.dts` and a board `.conf`.
  Zero source changes.
- CI build matrix is trivially extended: add a new row to the matrix with
  a new board name.
- AI-assisted development: include `prj.conf` in the agent's context, and it
  can correctly generate new library code that follows the same pattern.

**Harder:**
- `prj.conf` can become long for feature-rich apps. Structure it with inline
  comments grouping related symbols (see the annotated example in
  [`docs/architecture/composition-model.md`](../architecture/composition-model.md)).
- Kconfig `depends on` chains can produce surprising "invisible" effects when
  a dependency is disabled. Use `west build --cmake-only` + `ninja menuconfig`
  to inspect the resolved configuration.

**Constrained:**
- App `CMakeLists.txt` must never contain feature-selection logic. If a
  reviewer sees `if(SOME_CONDITION) target_link_libraries(...)` in an app
  `CMakeLists.txt`, it is a violation of this ADR. This rule is absolute —
  it applies to CMake only, not to C source in `apps/*/src/` (see "When
  app-level C code is acceptable" in
  [`docs/architecture/composition-model.md`](../architecture/composition-model.md)).
- Library `CMakeLists.txt` may only gate on `if(CONFIG_...)` — the direct
  Kconfig condition. No custom CMake variables.
- When in doubt, put it in a library. Extracting app-level code into a library
  later is cheap; untangling a bloated app is not.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| `target_link_libraries()` in app CMake | Feature composition split across CMake + Kconfig = two places to understand; CMake is harder to read than Kconfig; AI agents more often get CMake wrong |
| Zephyr modules as separate git repos | Over-engineering for a single-repo open-source project; west multi-repo management adds contributor friction |
| Single monolithic CMakeLists.txt | Does not scale; adding a feature requires editing a central file; no conditional compilation |
| Feature flags via C `#define` in source | Preprocessor flags visible only at C level; Kconfig dependency checking lost; harder to inspect from outside |

---

## See also

- Current implementation: [`docs/architecture/composition-model.md`](../architecture/composition-model.md) (SYS_INIT chain, library boundaries, 50-line main.c rule)
- Related ADRs: [ADR-001](ADR-001-repo-and-workspace-structure.md) (T2 topology), [ADR-002](ADR-002-zbus-as-system-bus.md) (zbus channels as library boundary)
