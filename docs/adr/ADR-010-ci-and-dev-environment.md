# ADR-010 — CI/CD and Developer Environment

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-02-21 |
| **Deciders** | Project founder |

---

## Context

Zephyr development has historically had a high barrier to entry. Setting up
the SDK, toolchains, Python dependencies, and west takes 30–60 minutes on a
fresh machine and frequently breaks across host OS versions. For an open-source
project targeting contributors who may not be Zephyr experts, this friction
directly reduces contribution rate.

Additionally, the project uses AI-assisted development (Claude as a coding
agent). The agent needs a reproducible, known-good environment where it can
verify that generated code compiles and tests pass — without access to the
developer's machine.

Two environment concerns must be addressed simultaneously:
1. **Local development** — VS Code devcontainer, instant setup, full toolchain
2. **CI** — GitHub Actions, same container, deterministic builds and tests

Using two different environments for local and CI has historically caused
"works on my machine" failures. The decision must use the **same image**
for both.

---

## Decision

Use **`ghcr.io/tobiwan88/zephyr_docker`** as the single container image for
both local development (VS Code devcontainer) and CI (GitHub Actions). This
image is maintained by the project founder and can be updated to add missing
tools via a PR to `tobiwan88/zephyr_docker`.

### Container image details

| Property | Value |
|----------|-------|
| Image | `ghcr.io/tobiwan88/zephyr_docker:latest` |
| Base | Debian Trixie Slim |
| Zephyr SDK | v0.17.4 |
| Zephyr version | v4.3.0 |
| User | `zephyr` (UID 1000, non-root) |
| Python venv | `/home/zephyr/.venv` |
| SDK path | `/home/zephyr/zephyr-sdk` |
| Workspace (west topdir) | `/home/zephyr/workspace` |
| Zephyr source | `/home/zephyr/workspace/zephyr` |
| Gateway binary | `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe` |
| West activation | `source ~/.venv/bin/activate` required before all west commands |

Tag strategy:
- `:latest` — all toolchains (~2 GB) — use for development
- `:arm` — ARM Cortex-M only (~500 MB) — use in CI once hardware target is confirmed
- `:arm-riscv64` — ARM + RISC-V (~900 MB)

### Local development: VS Code devcontainer

```
Developer workflow:
─────────────────
git clone → open in VS Code → "Reopen in Container"
                                    │
                    onCreateCommand runs once:
                    west init -l . && west update
                                    │
                    Container ready with:
                    ├── Full Zephyr SDK
                    ├── west + Python tools
                    ├── All VS Code extensions
                    ├── pre-commit hooks installed
                    └── Named volume for west module cache
```

The west module cache is stored in a named Docker volume
(`weather-station-west-cache`). This persists across container rebuilds —
developers don't re-download ~500 MB of Zephyr modules when the image updates.

Key VS Code extensions installed automatically:
- `trond-snekvik.devicetree` — DTS syntax + schema validation
- `trond-snekvik.kconfig` — Kconfig symbol completion
- `marus25.cortex-debug` — OpenOCD / J-Link debug
- `ms-vscode.cpptools-extension-pack` — IntelliSense

### CI: GitHub Actions

```yaml
jobs:
  build:
    container:
      image: ghcr.io/tobiwan88/zephyr_docker:latest
      options: --user zephyr     ← same user as devcontainer
    steps:
      - uses: actions/checkout@v4
      - run: |
          source ~/.venv/bin/activate   ← same activation as devcontainer
          west init -l .
          west update --narrow -o=--depth=1
      - run: |
          source ~/.venv/bin/activate
          ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
            west build -b native_sim/native/64 weather-station/apps/gateway
```

Every `run:` step activates the venv first — exactly as in the devcontainer
terminal. The same `west build` command used locally works in CI without
modification.

**`ZEPHYR_BASE` override — critical:** The default `ZEPHYR_BASE` in the
container image points to a path that does not exist in the west T2 workspace
layout. All `west build` and `west twister` commands must be prefixed with
`ZEPHYR_BASE=/home/zephyr/workspace/zephyr`. If a build fails with a missing
Zephyr path, also delete `build/CMakeCache.txt` — it caches the old value.

**Board target:** Always use `native_sim/native/64`. The shorthand `native_sim`
selects a 32-bit variant and produces a different binary path.

### CI pipeline structure

```
push / PR to master or feature branches
         │
         ▼
┌────────────────────────────────────────────────────────────┐
│  job: lint                                                 │
│  ├── pre-commit run --all-files                            │
│  │     ├── trailing-whitespace                             │
│  │     ├── clang-format (via .clang-format)               │
│  │     ├── zephyr-checkpatch-diff                         │
│  │     └── yamllint                                        │
│  └── Passes → unblocks build job                          │
└──────────────────┬─────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: build (matrix)                                       │
│  ├── gateway / native_sim                                  │
│  ├── sensor-node / native_sim                              │
│  └── (future) gateway / esp32_devkitc_wroom                │
│  Artifacts: zephyr.elf, zephyr.exe, .config               │
└──────────────────┬─────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: test-native-sim                                      │
│  ZEPHYR_BASE=.../zephyr west twister                       │
│    -p native_sim/native/64 -T weather-station/tests/      │
│  ├── tests/q31/           (Q31 round-trip accuracy)        │
│  ├── tests/sensor_event/  (zbus pub/sub)                   │
│  └── tests/fake_sensors/  (trigger→publish, shell set)     │
│  Artifact: twister.xml (JUnit format → GitHub PR checks)   │
└──────────────────┬─────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: renode-integration  (DISABLED — Phase 2)             │
│  antmicro/renode-test-action                               │
│  simulation/weather_test.robot                             │
└────────────────────────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: ci-success  (required status check)                  │
│  Aggregates all job results — blocks PR merge on failure   │
└────────────────────────────────────────────────────────────┘
```

### Code quality tools

| Tool | Config file | What it checks |
|------|------------|----------------|
| `clang-format` | `.clang-format` | C code formatting (Zephyr style, 8-space tabs, 100-col) |
| `checkpatch.pl` | (Zephyr built-in) | Zephyr coding style, commit format |
| `yamllint` | `.yamllint.yml` | west.yml, CI workflows, DT bindings |
| `pre-commit` | `.pre-commit-config.yaml` | Runs all above + file hygiene hooks |

Pre-commit runs on every `git commit` locally (installed by devcontainer
`postCreateCommand`) and on every push in CI. The same checks, same config.

### Adding a missing tool to the container

If a tool is missing from `tobiwan88/zephyr_docker`:

1. Open a PR to `https://github.com/tobiwan88/zephyr_docker`
2. Add the package to the `Dockerfile`
3. Once the image is rebuilt and pushed, update the `image:` tag in
   `devcontainer.json` and `ci.yml` if a new version tag is used

For temporary local use: `sudo apt-get install -y <package>` in a running
container (lost on rebuild).

---

## Consequences

**Easier:**
- Zero-config onboarding: `git clone` + VS Code "Reopen in Container" = ready
  to build in ~5 minutes (mostly west update download time).
- "Works on my machine" problems are eliminated — local and CI use the same image.
- Adding a new CI check = add a pre-commit hook. It runs locally and in CI
  automatically.
- AI agent can verify its output within the same devcontainer without any
  additional setup.

**Harder:**
- The devcontainer requires Docker running on the host (Docker Desktop on Mac/Windows,
  Docker Engine on Linux). On some corporate environments Docker may not be allowed.
- The named volume for west modules can grow stale after a major Zephyr version bump.
  Developers must occasionally `docker volume rm weather-station-west-cache`.
- Container image updates must be tested before being rolled out — a broken image
  blocks all developers simultaneously.

**Constrained:**
- The project is coupled to the `tobiwan88/zephyr_docker` image maintenance.
  If the image falls behind Zephyr versions, a migration to the official
  `ghcr.io/zephyrproject-rtos/ci` image must be possible with minimal changes
  (the only difference is venv activation and user name).
- CI runners must be GitHub-hosted (Linux x86_64). Self-hosted runners require
  Docker support and outbound GHCR access.

---

## Alternatives considered

| Alternative | Rejected because |
|-------------|-----------------|
| Official `ghcr.io/zephyrproject-rtos/ci` image | Larger, less control over toolchain versions, can't add project-specific tools without a fork |
| Host-native Zephyr SDK install | Different SDK versions across developer machines; breaks CI reproducibility; setup friction for contributors |
| Codespaces (GitHub cloud dev environment) | Good alternative but adds GitHub billing dependency; devcontainer config is compatible so migration is trivial |
| Nix/NixOS reproducible environment | Excellent reproducibility but very high contributor learning curve; not worth the friction for a hobbyist open-source project |
| Docker Compose for multi-service local dev | Useful if Mosquitto broker needs to be part of the compose stack — keep as a future option for running full stack locally |
