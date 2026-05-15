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

For the container image property table, devcontainer workflow, CI job YAML,
ZEPHYR_BASE override, CI pipeline structure, and code quality tools table, see
[`docs/architecture/ci-dev-environment.md`](../architecture/ci-dev-environment.md).

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

---

## See also

- Current implementation: `.devcontainer/`, `.github/workflows/ci.yml`, `.pre-commit-config.yaml`
- Related ADRs: [ADR-009](ADR-009-native-sim-first.md) (native_sim first), [ADR-012](ADR-012-integration-test-architecture.md) (integration tests in CI)
