# FRDM-MCXN947 Gateway Board Support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add FRDM-MCXN947 as a hardware target for the gateway app with real Ethernet and fake sensors.

**Architecture:** Two board-specific files (DT overlay + Kconfig fragment) under `apps/gateway/boards/`. Zephyr auto-applies them when building with `-b frdm_mcxn947`. No changes to `main.c` or any library code.

**Tech Stack:** Zephyr v4.4.0, MCUX ENET driver, lwIP-style Zephyr networking (IPv4 + DHCP), fake sensor drivers.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `apps/gateway/boards/frdm_mcxn947.overlay` | Create | DT nodes for 6 fake sensors (same UIDs as native_sim) |
| `apps/gateway/boards/frdm_mcxn947.conf` | Create | Kconfig: real Ethernet, disable LVGL, tune memory |

No other files change. `main.c` already guards LVGL behind `#if CONFIG_LVGL_DISPLAY`.

---

### Task 1: Create DeviceTree overlay

**Files:**
- Create: `apps/gateway/boards/frdm_mcxn947.overlay`

- [ ] **Step 1: Write the DT overlay**

Create `apps/gateway/boards/frdm_mcxn947.overlay` with the same 6 fake sensor nodes as `native_sim.overlay` (no SDL display section):

```dts
/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Gateway DT overlay for frdm_mcxn947.
 *
 * Defines six fake sensors (simulated on hardware):
 *   - Two indoor sensors (living room): temperature + humidity
 *   - Two outdoor sensors: temperature + humidity
 *   - Indoor CO2 + VOC
 *
 * UIDs 0x0001–0x000F are reserved for gateway local sensors.
 * UIDs 0x0011–0x001F are reserved for gateway outdoor sensors.
 * UIDs 0x0021+ are for remote sensor nodes.
 *
 * No display — LVGL is disabled on this target.
 */

/ {
	fake_temp_indoor: fake-temp-indoor {
		compatible = "fake,temperature";
		sensor-uid = <0x0001>;
		initial-value-mdegc = <21000>;
		status = "okay";
	};

	fake_hum_indoor: fake-hum-indoor {
		compatible = "fake,humidity";
		sensor-uid = <0x0002>;
		initial-value-mpct = <50000>;
		status = "okay";
	};

	fake_temp_outdoor: fake-temp-outdoor {
		compatible = "fake,temperature";
		sensor-uid = <0x0011>;
		initial-value-mdegc = <4000>;
		status = "okay";
	};

	fake_hum_outdoor: fake-hum-outdoor {
		compatible = "fake,humidity";
		sensor-uid = <0x0012>;
		initial-value-mpct = <30000>;
		status = "okay";
	};

	fake_co2_indoor: fake-co2-indoor {
		compatible = "fake,co2";
		sensor-uid = <0x0003>;
		initial-value-mppm = <800000>;
		status = "okay";
	};

	fake_voc_indoor: fake-voc-indoor {
		compatible = "fake,voc";
		sensor-uid = <0x0004>;
		initial-value-miaq = <25000>;
		status = "okay";
	};
};
```

- [ ] **Step 2: Commit**

```bash
git add apps/gateway/boards/frdm_mcxn947.overlay
git commit -m "feat(gateway): add frdm_mcxn947 DT overlay with fake sensors"
```

---

### Task 2: Create Kconfig fragment

**Files:**
- Create: `apps/gateway/boards/frdm_mcxn947.conf`

- [ ] **Step 1: Write the Kconfig fragment**

Create `apps/gateway/boards/frdm_mcxn947.conf`. This fragment is applied by Zephyr **after** `prj.conf`, so `=n` settings override prj.conf values:

```kconfig
# FRDM-MCXN947 gateway configuration
# Applied after prj.conf — overrides native_sim-specific settings.

# ---------------------------------------------------------------
# Networking: real Ethernet (replaces native offloaded sockets)
# ---------------------------------------------------------------
CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=n
CONFIG_NET_NATIVE_OFFLOAD=n

CONFIG_NET_L2_ETHERNET=y
CONFIG_ETH_NXP_ENET=y

CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
CONFIG_NET_CONFIG_AUTO_INIT=y
CONFIG_NET_CONFIG_NEED_IPV4=y

# Network stack buffers
CONFIG_NET_MAX_CONTEXTS=8
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y

# DNS resolver
CONFIG_DNS_RESOLVER=y
CONFIG_DNS_RESOLVER_ADDITIONAL_BUF_CTR=2

# Network shell + stats
CONFIG_NET_SHELL=y
CONFIG_NET_STATISTICS=y
CONFIG_NET_STATISTICS_USER_API=y

# ---------------------------------------------------------------
# Disable display stack (no hardware display on this board)
# ---------------------------------------------------------------
CONFIG_LVGL_DISPLAY=n
CONFIG_LVGL=n
CONFIG_DISPLAY=n
CONFIG_INPUT=n

# ---------------------------------------------------------------
# Memory tuning (512 KB RAM, Cortex-M33)
# ---------------------------------------------------------------
CONFIG_HEAP_MEM_POOL_SIZE=16384
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
```

Key decisions:
- `CONFIG_NET_MAX_CONTEXTS=8` — enough for HTTP (listen + 3 clients = 4), MQTT (1), SNTP (1), DNS (1), margin (1). Lower than native_sim's 16 because we don't have ZVFS epoll collision concerns.
- `CONFIG_HEAP_MEM_POOL_SIZE=16384` — half of native_sim's 32768 to fit in 512 KB RAM alongside the stack.
- No `CONFIG_ZVFS_POLL_MAX` — that's native_sim-specific (NSOS epoll). Real hardware uses standard Zephyr networking.
- `CONFIG_NET_CONFIG_NEED_IPV4=y` — blocks boot until IPv4 is configured (DHCP succeeds).

- [ ] **Step 2: Commit**

```bash
git add apps/gateway/boards/frdm_mcxn947.conf
git commit -m "feat(gateway): add frdm_mcxn947 Kconfig with real Ethernet"
```

---

### Task 3: Build verification

**Files:**
- No file changes — build and smoke-test only.

- [ ] **Step 1: Build gateway for frdm_mcxn947**

```bash
west build -p always -b frdm_mcxn947 apps/gateway
```

Expected: clean build with no errors. If the build fails with missing symbols, check:
- `CONFIG_ETH_NXP_ENET` depends on `DT_HAS_NXP_ENET_MAC_ENABLED` — the board DTS must have `&enet_mac` with `status = "okay"` (it does for the cpu0 variant)
- If CMakeCache has stale paths, delete `build/` and retry

- [ ] **Step 2: Verify binary exists**

```bash
ls -la build/frdm_mcxn947/zephyr/zephyr.bin
ls -la build/frdm_mcxn947/zephyr/zephyr.elf
```

Expected: both files present. `.bin` is for flashing, `.elf` is for debugging.

- [ ] **Step 3: Verify native_sim build is not broken**

```bash
west build -p always -b native_sim/native/64 apps/gateway
```

Expected: clean build. The new board files must not affect the native_sim build.

- [ ] **Step 4: Run native_sim smoke-test**

```bash
printf "help\nfake_sensors list\nkernel uptime\n" | \
  timeout 10 /home/zephyr/workspace/build/native_sim_native_64/gateway/zephyr/zephyr.exe \
  -uart_stdinout 2>&1
```

Expected: `help` lists `fake_sensors` command, `fake_sensors list` shows 6 sensors.

- [ ] **Step 5: Run full native_sim test suite**

```bash
mosquitto -p 1883 -d 2>/dev/null || true
ZEPHYR_BASE=/home/zephyr/workspace/zephyr \
  west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N
```

Expected: all tests pass. No regressions from the new board files.

- [ ] **Step 6: Run pre-commit**

```bash
pre-commit run --all-files
```

Expected: all checks pass.

---

## Self-Review

**Spec coverage check:**
- [x] DT overlay with 6 fake sensors → Task 1
- [x] Kconfig: real Ethernet, DHCP → Task 2
- [x] Kconfig: disable LVGL → Task 2
- [x] Kconfig: memory tuning → Task 2
- [x] Build verification → Task 3
- [x] Native_sim regression check → Task 3
- [x] No changes to main.c → confirmed (already guarded)

**Placeholder scan:** No TBD, TODO, or vague steps. All code blocks are complete.

**Type consistency:** N/A — no code types, only config and DT.
