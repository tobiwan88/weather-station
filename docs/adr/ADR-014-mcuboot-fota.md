# ADR-014 — MCUboot FOTA and Secure Firmware Update

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-05-09 |
| **Deciders** | Project founder |

---

## Context

The gateway running on `frdm_mcxn947` needs a reliable over-the-air firmware update
mechanism. Without it, every firmware change requires physical access and a JTAG/USB
cable. The sensor network also needs a path to update remote nodes (STM32WLE5JC /
LoRa E5-Mini) once the wireless transport is implemented.

Requirements derived from this project's goals:

1. **Reliable** — a failed or corrupt update must never brick the device; automatic
   rollback to the previous image is mandatory.
2. **Authenticated** — only images signed with a known private key may be installed.
3. **Two transports** — UART (development, recovery) and HTTP upload via the existing
   dashboard (normal field update path).
4. **Sensor-node relay** — the gateway must eventually be able to forward firmware to
   remote nodes; the interface boundary must be defined now even if not implemented.

---

## Decision

Use **MCUboot as a standalone bootloader** (no TF-M) on `frdm_mcxn947`, with
**ED25519 image signing** and **MCUmgr** as the update management protocol.

The gateway build target for FOTA-capable firmware is `frdm_mcxn947/mcxn947/cpu0`
(the standard non-TrustZone variant), built via Zephyr sysbuild so that MCUboot and
the application are produced together.

FOTA upload is exposed over two transports:
- **UART** — MCUmgr SMP over serial (existing DTS node `zephyr,uart-mcumgr`)
- **HTTP** — new `/api/fota/*` endpoints in `lib/http_dashboard`, gated by existing
  session/bearer auth, streaming image data directly to flash via `flash_img` API.

A/B slot swap uses MCUboot's **SWAP_MOVE** mode (no scratch partition required). The
new image must call `boot_write_img_confirmed()` before the next reset to become
permanent. The application waits a 5-second settle delay before confirming; if it
crashes or hangs before that call, MCUboot reverts to the previous slot on the next
reset.

MCUboot standalone was chosen over TF-M because the default TF-M layout restricts the
Zephyr NS image to 128 KB RAM — insufficient for the gateway stack (HTTP, MQTT, zbus,
shell, sensor registry require ~320 KB). Patching TF-M's SAU configuration to extend NS
RAM is fragile across TF-M upgrades and over-engineered for a home project.

ED25519 was chosen over RSA for compact 64-byte signatures, fast Cortex-M33
verification, and native `imgtool` support.

SWAP_MOVE was chosen because it performs the A/B exchange without a dedicated scratch
partition, making full use of all internal flash.

HTTP transport for FOTA uploads was integrated into `lib/http_dashboard` (not a
separate MCUmgr UDP port) to reuse the existing session/bearer auth layer and avoid
opening a second unauthenticated port, consistent with ADR-008 and ADR-011.

---

## Accepted Security Trade-offs

This is a home project. The following security properties are **intentionally omitted**
and must be revisited before any production or CRA-regulated deployment:

| Property | Status | Required for production |
|---|---|---|
| Hardware key isolation | **Not implemented** | TF-M ITS or secure element required |
| Anti-rollback NV counter | **Disabled** | Enable `CONFIG_BOOT_ROLLBACK_PROT` |
| Encrypted image slots | **Not implemented** | MCUboot image encryption |
| Attestation / device identity | **Not implemented** | TF-M attestation service |

The ED25519 private signing key is held offline and never stored in firmware. The
MCUboot public key embedded in the bootloader is in software flash and is extractable
by a physical attacker with flash-read access. This risk is accepted for a device on
a private home network.

Development keys are generated locally by `scripts/gen-dev-key.sh` and stored in a
`.gitignore`d `keys/` directory. Keys are **never committed to the repository**.

---

## Alternatives Considered

### Full TF-M + BL2 (Option B)

TF-M with `BL2=ON` provides hardware key isolation via TrustZone and PSA ITS. The full
trust chain would be:

```
NXP ROM Bootloader → MCUboot BL2 (TF-M Secure world)
  → TF-M SPE (PSA Crypto, ITS) → Zephyr NSPE
```

**Rejected for this project** because the default TF-M flash/RAM layout leaves only
256 KB flash and 128 KB RAM for the Zephyr NS image — insufficient for the gateway
stack. This option remains the correct long-term path for any production deployment.

### MCUboot + TF-M with custom SAU

Extend the NS RAM by patching TF-M's SAU configuration to include `sramg` / `sramh`.
**Rejected** as over-engineering: the SAU patch is fragile across TF-M upgrades, and
`sramg` (0x20050000, 64 KB) is allocated to CPU1 in the standard board configuration.

### SMP over UDP (MCUmgr network transport)

Use `CONFIG_MCUMGR_TRANSPORT_UDP` for network-based updates instead of embedding
the upload in the HTTP dashboard. **Rejected** because it opens a second unauthenticated
port; the HTTP dashboard already has session/bearer auth that can gate FOTA uploads.

### NXP ROM Bootloader dual-bank without MCUboot

Use the ROM ISP dual-bank mechanism directly, without MCUboot, to perform slot swaps.
**Rejected** because MCUmgr's `img mgmt` subsystem is designed for MCUboot's slot/
trailer format. Without MCUboot there is no standard A/B swap or rollback mechanism,
requiring custom implementation.

---

## Consequences

**Easier:**
- Reliable updates with automatic rollback protect the device from bad firmware.
- UART transport provides an unconditional recovery path without network access.
- HTTP transport integrates naturally with the existing dashboard and auth layer.
- Sysbuild produces MCUboot + signed app in one `west build` invocation.
- Key generation script ensures no keys are ever committed to the repo.

**Harder:**
- The application must actively confirm each update; a confirm bug causes repeated rollback even on a correct image.
- Private key management is the operator's responsibility; loss of the key means no further authenticated updates.

**Constrained:**
- MCUboot reduces available flash per image slot; the gateway feature set must stay within the slot budget.
- native_sim target is unaffected — MCUboot and FOTA are hardware-only features (`depends on BOOTLOADER_MCUBOOT`).

---

## See also

- Current implementation: [`docs/architecture/firmware-update.md`](../architecture/firmware-update.md) (boot chain, flash layout, key management, MCUmgr transports)
- Related ADRs: [ADR-008](ADR-008-kconfig-app-composition.md) (Kconfig composition), [ADR-011](ADR-011-http-dashboard.md) (HTTP auth layer), [ADR-006](ADR-006-lora-channel-boundary.md) (sensor-node relay, deferred)
