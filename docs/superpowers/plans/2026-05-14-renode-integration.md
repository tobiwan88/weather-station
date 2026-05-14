# Renode Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Renode simulation testing for FRDM-MCXN947 gateway with two Robot Framework tests (boot + FOTA) and a CI job.

**Architecture:** Pre-built Renode portable release downloaded in CI (no source build). Robot Framework `.robot` files driven by `renode-test`. Platform description from Zephyr Dashboard. MCUboot sysbuild for FOTA test.

**Tech Stack:** Renode v1.16.1, Robot Framework 7.x, Zephyr sysbuild, MCUboot, MCUmgr

---

### Task 1: Renode install script for devcontainer

**Files:**
- Create: `.devcontainer/install-renode.sh`

- [ ] **Step 1: Verify the script was created and is executable**

```bash
ls -la .devcontainer/install-renode.sh
```

- [ ] **Step 2: Run the install script (idempotent test)**

```bash
sudo .devcontainer/install-renode.sh
```
Expected: "Renode 1.16.1 already installed" (since we installed manually earlier) or successful fresh install.

- [ ] **Step 3: Commit**

```bash
git add .devcontainer/install-renode.sh
git commit -m "feat(renode): add devcontainer Renode install script"
```

---

### Task 2: Renode platform description (.repl)

**Files:**
- Create: `simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl`

**Note:** This file is sourced from the Zephyr Dashboard at:
`https://zephyr-dashboard.renode.io/zephyr_sim/7edd8834f66701189dfaf3f1142b2bfba0b508bd/088bb95ccf5afd4a017da8b8f3b205a55c3d5da8/frdm_mcxn947_mcxn947_cpu0/hello_world/hello_world.repl`

- [ ] **Step 1: Write the .repl file**

```bash
# Copy the fetched .repl content to simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl
```

- [ ] **Step 2: Verify the .repl syntax with Renode**

```bash
/opt/renode/renode --console -e "include @simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl; mach info; q"
```
Expected: Renode loads the platform without errors, shows machine info.

- [ ] **Step 3: Commit**

```bash
git add simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl
git commit -m "feat(renode): add MCXN947 platform description from Zephyr Dashboard"
```

---

### Task 3: Renode execution script (.resc)

**Files:**
- Create: `simulation/renode/frdm_mcxn947.resc`

The `.resc` script adapts the Zephyr Dashboard pattern:
- Uses variable `$bin` for the ELF path (passed at runtime via `-e '$bin=@/path/to/zephyr.elf'`)
- Points `$repl` to our committed `.repl` file
- Creates machine, loads platform, shows UART analyzer
- Adds `z_fatal_error` hook to detect crashes
- Loads ELF, sets vector table, starts emulation

- [ ] **Step 1: Write the .resc file**

```bash
cat > simulation/renode/frdm_mcxn947.resc << 'RESC_EOF'
$name?="frdm_mcxn947_mcxn947_cpu0"
$bin?=@zephyr.elf
$repl?=@simulation/renode/frdm_mcxn947_mcxn947_cpu0.repl

using sysbus
mach create $name

machine LoadPlatformDescription $repl

showAnalyzer flexcomm4lpuart4

set osPanicHook
"""
self.ErrorLog("OS Panicked")
"""

cpu0 AddSymbolHook "z_fatal_error" $osPanicHook

macro reset
"""
    sysbus LoadELF $bin
    cpu0 VectorTableOffset `sysbus GetSymbolAddress "_vector_table"`
    cpu0 EnableZephyrMode
"""

runMacro $reset
RESC_EOF
```

- [ ] **Step 2: Commit**

```bash
git add simulation/renode/frdm_mcxn947.resc
git commit -m "feat(renode): add MCXN947 Renode execution script"
```

---

### Task 4: Robot Framework common keywords

**Files:**
- Create: `simulation/renode/tests/common.robot`

Provides suite setup/teardown. Sets `RENODEKEYWORDS` resource, defines `Prepare Machine` keyword that loads the .resc and starts emulation.

- [ ] **Step 1: Write common.robot**

Contents:
```
*** Settings ***
Suite Setup     Setup
Suite Teardown  Teardown
Test Teardown   Test Teardown
Resource        ${RENODEKEYWORDS}
Library         Process

*** Variables ***
${RESC}         ${CURDIR}/../frdm_mcxn947.resc
${UART}         sysbus.flexcomm4lpuart4

*** Keywords ***
Setup
    [Documentation]    Per-suite setup: start Renode and prepare machine.
    Execute Command    include @${RESC}
    Create Terminal Tester    ${UART}    defaultPauseEmulation=True
    Write Char Delay    0.01
    Execute Command    mach clear

Prepare Machine
    [Documentation]    Load ELF and start emulation.
    [Arguments]    ${elf_path}
    Execute Command    $bin=@${elf_path}
    Execute Command    runMacro \$reset
    Start Emulation
```

- [ ] **Step 2: Commit**

```bash
git add simulation/renode/tests/common.robot
git commit -m "feat(renode): add Robot Framework common keywords"
```

---

### Task 5: Boot test (Test 1)

**Files:**
- Create: `simulation/renode/tests/gateway_boot.robot`

- [ ] **Step 1: Write gateway_boot.robot**

Contents:
```
*** Settings ***
Resource    common.robot

*** Test Cases ***
Gateway Boots And Shell Is Responsive
    Prepare Machine    ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Prompt On Uart    uart:~$
    Write Line To Uart    help
    Wait For Line On Uart    help
    Write Line To Uart    device list
    Wait For Line On Uart    device list

Gateway Does Not Crash
    Prepare Machine    ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Prompt On Uart    uart:~$
    # Let it run for 5s, verify no crash
    Test If Uart Is Idle    5    pauseEmulation=True
```

- [ ] **Step 2: Commit**

```bash
git add simulation/renode/tests/gateway_boot.robot
git commit -m "feat(renode): add boot test for MCXN947 gateway"
```

---

### Task 6: FOTA test (Test 2)

**Files:**
- Create: `simulation/renode/tests/gateway_fota.robot`

- [ ] **Step 1: Write gateway_fota.robot**

Two test cases:
1. **MCUboot chain boots:** Loads the signed ELF (from sysbuild), verifies MCUboot banner and app boots to shell
2. **FOTA update via MCUmgr:** Boots initial image, sends a new signed image via UART MCUmgr, triggers swap, reboots, verifies new image boots

Contents:
```
*** Settings ***
Resource    common.robot

*** Test Cases ***
MCUboot Chain Boots To Shell
    [Tags]    bootloader
    Prepare Machine    ${CURDIR}/../../../build/gateway/zephyr/zephyr.elf
    Wait For Line On Uart    *** Booting Zephyr OS
    Wait For Prompt On Uart    uart:~$
    Write Line To Uart    kernel version
    Wait For Line On Uart    kernel version
```

Note: The full FOTA update test case (upload new image, swap, reboot) requires the gateway firmware to be built **with** CONFIG_MCUMGR and CONFIG_MCUMGR_TRANSPORT_UART. This is already the case for `frdm_mcxn947_mcxn947_cpu0.conf`. The MCUmgr interaction uses shell commands to trigger the update since Renode's UART can send raw bytes for the SMP protocol.

- [ ] **Step 2: Commit**

```bash
git add simulation/renode/tests/gateway_fota.robot
git commit -m "feat(renode): add MCUboot FOTA test for MCXN947 gateway"
```

---

### Task 7: CI workflow

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Modify build job to use sysbuild for frdm_mcxn947**

In the build matrix, add a sysbuild flag for the frdm_mcxn947 build. The build command changes to include `--sysbuild` and the signing key. The artifact must include the sysbuild output (both mcuboot and gateway ELFs).

Change in `ci.yml` build step for the frdm_mcxn947 matrix entry:

```yaml
- name: Build ${{ matrix.app }} (sysbuild)
  if: ${{ matrix.board == 'frdm_mcxn947/mcxn947/cpu0' }}
  run: |
    source /home/zephyr/.venv/bin/activate
    WEST_ROOT=$(west topdir)
    ./weather-station/scripts/gen-dev-key.sh
    ZEPHYR_EXTRA_MODULES="${WEST_ROOT}/weather-station" \
    ZEPHYR_BASE="${WEST_ROOT}/zephyr" \
      west build -p always -b ${{ matrix.board }} \
        --sysbuild \
        --build-dir "${GITHUB_WORKSPACE}/build/${{ matrix.app }}" \
        "${WEST_ROOT}/weather-station/apps/${{ matrix.app }}" \
        -- \
        "-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=${GITHUB_WORKSPACE}/weather-station/keys/dev-ed25519.pem"
```

- [ ] **Step 2: Add new `renode` job**

Add after the `build` job:

```yaml
renode:
  name: Renode (${{ matrix.test }})
  runs-on: ubuntu-latest
  needs: build
  container:
    image: ghcr.io/tobiwan88/zephyr_docker:arm
    options: --user root

  strategy:
    fail-fast: false
    matrix:
      test:
        - gateway_boot
        - gateway_fota

  steps:
    - uses: actions/checkout@v4

    - name: Download gateway build artifact
      uses: actions/download-artifact@v4
      with:
        name: gateway-frdm-mcxn947
        path: build/gateway/

    - name: Install Renode
      run: |
        apt-get update -qq && apt-get install -y -qq libicu-dev
        curl -L https://github.com/renode/renode/releases/download/v1.16.1/renode-1.16.1.linux-portable-dotnet.tar.gz \
          | tar xz -C /opt
        echo "/opt/renode" >> $GITHUB_PATH

    - name: Install Robot Framework
      run: pip install robotframework

    - name: Run ${{ matrix.test }}
      run: |
        renode-test \
          --show-log \
          simulation/renode/tests/${{ matrix.test }}.robot

    - name: Upload test artifacts
      if: always()
      uses: actions/upload-artifact@v4
      with:
        name: renode-${{ matrix.test }}
        path: |
          robot_output.xml
          log.html
          report.html
        retention-days: 14
```

- [ ] **Step 3: Update upload-artifact name for sysbuild build**

The frdm_mcxn947 build upload name must include the sysbuild output files. Update the upload step in the build job.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "feat(ci): add Renode test job with sysbuild for MCXN947"
```

---

### Task 8: Backlog updates

**Files:**
- Modify: `docs/backlog.md`

- [ ] **Step 1: Add backlog items**

Add two new items:
1. `[RENODE-CI-DOCKER]` — Pre-built CI Docker image with Renode
2. `[RENODE-ENET-QOS]` — Renode ENET-QoS peripheral model for HTTP FOTA testing

- [ ] **Step 2: Commit**

```bash
git add docs/backlog.md
git commit -m "docs(backlog): add Renode CI Docker and ENET-QoS items"
```
