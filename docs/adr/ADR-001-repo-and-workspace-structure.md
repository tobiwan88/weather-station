# ADR-001 — Repository and West Workspace Structure

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Zephyr projects can be organised in several topologies. The choice determines
how third-party modules are fetched, how the project registers its own drivers
and boards, and how a CI pipeline or new developer initialises a workspace from
scratch. Getting this wrong early forces painful refactoring once the project
grows.

Key constraints:
- The project must work as both a **west manifest** (the entry point for
  `west init`) and a **Zephyr module** (so its drivers, boards, and Kconfig
  are automatically discovered by the build system).
- It must stay lean — only fetch the Zephyr modules it actually needs.
- It must be immediately approachable for open-source contributors who have
  never used Zephyr before.
- It must be the model on which an AI coding agent generates new code, so the
  structure must be deterministic and unambiguous.

---

## Decision

Use Zephyr **T2 topology** (application-as-manifest): the `weather-station`
repo is simultaneously the west manifest repository *and* a Zephyr module.
`west init -l .` initialises from the local manifest; `west update` fetches
Zephyr and external modules.

The repo registers itself as a Zephyr module via `zephyr/module.yml`, which
tells the build system where to find custom boards, devicetree bindings, drivers,
and Kconfig. All application logic lives inside the repo — Zephyr and its
dependencies are fetched externally by west and never committed.

Apps compose features via `prj.conf` Kconfig symbols only. There are no
`target_link_libraries()` calls in app `CMakeLists.txt` and no manual include
paths. The `name-allowlist` in `west.yml` keeps the workspace lean — only the
modules actually needed are fetched.

For the directory layout, west manifest strategy, and how apps reference
libraries, see [`docs/architecture/system-overview.md`](../architecture/system-overview.md).

---

## Consequences

**Easier:**
- `west init -l .` + `west update` is the complete setup — one command from any
  CI runner or new developer machine.
- Adding a new library = add a directory under `lib/`, write `Kconfig` +
  `CMakeLists.txt`, done. Apps opt in via `prj.conf`.
- Custom boards and DT bindings are auto-discovered — no CMake wiring needed.

**Harder:**
- Contributors unfamiliar with west need to understand the T2 topology before
  they can reason about where files live.
- `ZEPHYR_BASE` must be set for IDE tooling (handled by devcontainer).

**Constrained:**
- Zephyr version is pinned in `west.yml`. Upgrading requires testing all apps
  and updating the `name-allowlist` if new modules are needed.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| T1 (Zephyr as manifest) | Project code would live as a module *under* Zephyr — confusing ownership, harder for contributors to find the entry point |
| T3 (separate manifest repo) | Extra repo to maintain; adds friction for a project that is itself open-source |
| Monorepo with Zephyr vendored | Unacceptably large repo; diverges from upstream Zephyr making security patches painful |
| nRF Connect SDK as base | NCS adds Nordic-specific layers not needed here; locks to Nordic hardware even for the ESP32 gateway target |

---

## See also

- Current implementation: `west.yml`, `zephyr/module.yml`, root `CMakeLists.txt` and `Kconfig`
- Current architecture: [`docs/architecture/system-overview.md`](../architecture/system-overview.md) (layers, library roles)
- Related ADRs: [ADR-008](ADR-008-kconfig-app-composition.md) (Kconfig-only composition)
