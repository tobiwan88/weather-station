# Hardware Documentation Update — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update `docs/hardware/README.md` with comprehensive hardware documentation for the FRDM-MCXN947 gateway, Seeed LoRa-E5 Mini sensor node, display shield, sensors, and initial setup guide.

**Architecture:** Single-file documentation update replacing placeholder sections with detailed component lists, pin connections, UART bridge architecture, and step-by-step setup instructions.

**Tech Stack:** Markdown documentation, Zephyr board definitions, hardware pin mapping.

---

### Task 1: Update docs/hardware/README.md

**Files:**
- Modify: `docs/hardware/README.md` (replace entire file)

- [ ] **Step 1: Write the updated README**

Replace the entire contents of `docs/hardware/README.md` with the content shown in the plan document. Key sections:

1. **Supported Targets** — native_sim, FRDM-MCXN947, Seeed LoRa-E5 Mini
2. **Gateway Hardware** — components table, display pin connections, UART bridge pin connections, debug console
3. **Sensor Node Hardware** — components, I2C sensor interfaces, UART bridge, debug console
4. **Initial Setup Guide** — 8 steps from requirements to verification
5. **Device Tree** — existing UID allocation table (preserved)

- [ ] **Step 2: Verify the file renders correctly**

Run: `wc -l docs/hardware/README.md`
Expected: ~200+ lines of documentation.

- [ ] **Step 3: Commit the documentation update**

```bash
git add docs/hardware/README.md
git commit -m "docs(hardware): add gateway and sensor node hardware documentation

Document FRDM-MCXN947 gateway components (display shield, LoRa-E5 Mini
UART bridge), Seeed LoRa-E5 Mini sensor node (BME688, SEN0460), pin
connections, and initial setup guide with jumper wiring instructions."
```

---

## Self-Review

1. **Spec coverage:** All brainstormed sections implemented — gateway hardware, sensor node hardware, pin connections, UART bridge concept (high-level), initial setup guide, device tree/UID allocation.
2. **Placeholder scan:** No TBD/TODO markers. All pin references derived from Zephyr board definitions.
3. **Consistency:** Pin mappings consistent between gateway and sensor node sections. UART bridge uses USART2 on LoRa-E5 Mini (PA2/PA3), leaving USART1 free for console.
4. **Scope:** Documentation-only change, single file. No code modifications.
