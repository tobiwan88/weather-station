# Renode Integration — Design Spec

> **Date:** 2026-05-14
> **Status:** Approved
> **Constrained by:** ADR-007, ADR-009, ADR-012, ADR-014

## Goal

Add Renode simulation testing for the FRDM-MCXN947 gateway, starting with two Robot Framework tests: (1) successful boot, and (2) MCUboot FOTA update via UART MCUmgr.

## Context

The project builds for `frdm_mcxn947/mcxn947/cpu0` in CI but never boots the firmware. The `[RENODE-PHASE2]` backlog item targets multi-node LoRa simulation, but the MCU is now selected (FRDM-MCXN947, ADR-014) and Renode has a pre-built platform description via the Zephyr Dashboard, making single-node testing feasible now.

## Decisions

### Testing framework: Robot Framework
Robot Framework is the standard Renode testing approach. Tests use `.robot` files run by `renode-test`. CI uses direct `renode-test` invocation (not `antmicro/renode-test-action` which builds Renode from source).

### CI strategy
Download the pre-built Renode portable tarball in CI (~30s) rather than building from source (~10-15 min). This avoids the overhead of the `renode-test-action` source build. A backlog item `[RENODE-CI-DOCKER]` tracks future optimization via a custom Docker image.

### Platform description
Use the Zephyr Dashboard-generated `.repl` file for `frdm_mcxn947_mcxn947_cpu0` as the authoritative platform description. Commit a copy to `simulation/renode/`.

### FOTA transport for Test 2
Use MCUmgr over UART (LPUART4 at 0x500B4000). The HTTP FOTA path is blocked by the lack of an ENET-QoS Renode peripheral model, and will be added as follow-up work.

### Build artifact
The CI `build (frdm-mcxn947)` job must be modified to use `--sysbuild` so it produces both MCUboot and the signed application ELF. The unsigned ELF from a non-sysbuild build cannot test MCUboot FOTA.

### Local setup
A `.devcontainer/install-renode.sh` script downloads and installs the Renode portable release to `/opt/renode`. Designed for one-time sudo invocation in the persistent devcontainer.

## Architecture

```
simulation/renode/
├── frdm_mcxn947_mcxn947_cpu0.repl    ← Platform description (from Zephyr Dashboard)
├── frdm_mcxn947.resc                 ← Execution script (loads ELF, starts emulation)
└── tests/
    ├── common.robot                  ← Suite setup/teardown keywords
    ├── gateway_boot.robot            ← Test 1: successful boot + shell check
    └── gateway_fota.robot            ← Test 2: MCUboot boot + MCUmgr FOTA update
```

## File manifest

| Action | File | Purpose |
|--------|------|---------|
| NEW | `.devcontainer/install-renode.sh` | Install Renode portable release locally |
| NEW | `simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl` | Platform description (from Zephyr Dashboard) |
| NEW | `simulation/renode/frdm_mcxn947.resc` | Renode execution script |
| NEW | `simulation/renode/tests/common.robot` | Shared Robot Framework keywords |
| NEW | `simulation/renode/tests/gateway_boot.robot` | Test 1: boot + no crash |
| NEW | `simulation/renode/tests/gateway_fota.robot` | Test 2: MCUboot chain + FOTA update |
| EDIT | `.github/workflows/ci.yml` | New `renode` job + sysbuild in build job |
| EDIT | `docs/backlog.md` | `[RENODE-CI-DOCKER]` + `[RENODE-ENET-QOS]` items |

## Out of scope

- HTTP FOTA testing (needs ENET-QoS Renode model)
- Multi-node LoRa simulation (Phase 2 backlog)
- Twister integration (deferred)
- LVGL/display testing (no display on hardware build)
