# CI and Developer Environment

> Design rationale: [ADR-010](../adr/ADR-010-ci-and-dev-environment.md).

The project uses a single container image — `ghcr.io/tobiwan88/zephyr_docker` — for both local development (VS Code devcontainer) and CI (GitHub Actions). This eliminates "works on my machine" failures: the same image, same activation steps, same commands work in both environments.

---

## Container image

| Property | Value |
|----------|-------|
| Image | `ghcr.io/tobiwan88/zephyr_docker:latest` |
| Base | Debian Trixie Slim |
| Zephyr SDK | v1.0.1 |
| Zephyr version | v4.4.0 |
| User | `zephyr` (UID 1000, non-root) |
| Python venv | `/home/zephyr/.venv` |
| SDK path | `/home/zephyr/zephyr-sdk` |
| Workspace (west topdir) | `/home/zephyr/workspace` |
| Zephyr source | `/home/zephyr/workspace/zephyr` |
| Gateway binary | `/home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe` |
| West activation | `source ~/.venv/bin/activate` required before all west commands |

**Tag strategy:**
- `:latest` — all toolchains (~2 GB) — use for development
- `:arm` — ARM Cortex-M only (~500 MB) — use in CI once hardware target is confirmed
- `:arm-riscv64` — ARM + RISC-V (~900 MB)

---

## Local development: VS Code devcontainer

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

The west module cache is stored in a named Docker volume (`weather-station-west-cache`). This persists across container rebuilds — developers don't re-download ~500 MB of Zephyr modules when the image updates.

Key VS Code extensions installed automatically:
- `trond-snekvik.devicetree` — DTS syntax + schema validation
- `trond-snekvik.kconfig` — Kconfig symbol completion
- `marus25.cortex-debug` — OpenOCD / J-Link debug
- `ms-vscode.cpptools-extension-pack` — IntelliSense

---

## CI: GitHub Actions

Every `run:` step activates the venv first — exactly as in the devcontainer terminal. The same `west build` command used locally works in CI without modification:

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

---

## ZEPHYR_BASE override

The default `ZEPHYR_BASE` in the container image points to a path that does not exist in the west T2 workspace layout. All `west build` and `west twister` commands must be prefixed with `ZEPHYR_BASE=/home/zephyr/workspace/zephyr`.

If a build fails with a missing Zephyr path, also delete `build/CMakeCache.txt` — it caches the old value.

**Board target:** Always use `native_sim/native/64`. The shorthand `native_sim` selects a 32-bit variant and produces a different binary path.

---

## CI pipeline structure

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
│  Unit tests (ztest, harness: ztest):                       │
│  ├── tests/fake_sensors/        (trigger→publish, shell)   │
│  ├── tests/sensor_event/        (zbus pub/sub)             │
│  ├── tests/mqtt_publisher/      (MQTT formatting)          │
│  ├── tests/remote_sensor_uid/   (UID generation)           │
│  └── tests/remote_sensor_manager/ (discovery flow)         │
│  Integration tests (pytest, harness: pytest):              │
│  └── tests/integration/         (ADR-012)                  │
│       ├── shell interaction     (smoke, sensors, config)   │
│       ├── HTTP API validation   (endpoints, JSON schema)   │
│       ├── MQTT flow             (skipped if no broker)     │
│       └── E2E data flow         (trigger→HTTP, trigger→MQTT│
│  Artifact: twister.xml (JUnit format → GitHub PR checks)   │
└──────────────────┬─────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: renode                                             │
│  Builds gateway with Renode Kconfig + trace recorder      │
│  Runs Robot Framework tests (boot, FOTA, trace)           │
│  Publishes trace artifacts (trace.bin, .perfetto, ELF)    │
│  Retention: 7 days                                        │
└──────────────────────────┬─────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────┐
│  job: deploy-results                                       │
│  Parses trace → Perfetto JSON; deploys to gh-pages         │
│  Deep-link: ui.perfetto.dev/#!/?url=.../traces/<ref>/...   │
│  Also deploys test results + coverage to tests/<ref>/       │
│  Runs on push to master and PRs only                        │
└────────────────────────────────────────────────────────────┘
                   │
                   ▼
┌────────────────────────────────────────────────────────────┐
│  job: ci-success  (required status check)                  │
│  Aggregates all job results — blocks PR merge on failure   │
└────────────────────────────────────────────────────────────┘
```

---

## Code quality tools

| Tool | Config file | What it checks |
|------|------------|----------------|
| `clang-format` | `.clang-format` | C code formatting (Zephyr style, 8-space tabs, 100-col) |
| `checkpatch.pl` | (Zephyr built-in) | Zephyr coding style, commit format |
| `yamllint` | `.yamllint.yml` | west.yml, CI workflows, DT bindings |
| `pre-commit` | `.pre-commit-config.yaml` | Runs all above + file hygiene hooks |

Pre-commit runs on every `git commit` locally (installed by devcontainer `postCreateCommand`) and on every push in CI. The same checks, same config.

---

## Adding a missing tool to the container

If a tool is missing from `tobiwan88/zephyr_docker`:

1. Open a PR to `https://github.com/tobiwan88/zephyr_docker`
2. Add the package to the `Dockerfile`
3. Once the image is rebuilt and pushed, update the `image:` tag in `devcontainer.json` and `ci.yml` if a new version tag is used

For temporary local use: `sudo apt-get install -y <package>` in a running container (lost on rebuild).
