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
- **HTTP** — new `/api/fota/*` endpoints in `lib/http_dashboard`, gated by existing
  session/bearer auth, streaming image data directly to flash via `flash_img` API.
  This is the **primary, permanent** update path.
- **UART** — MCUmgr SMP over serial (existing DTS node `zephyr,uart-mcumgr`).
  This is a **temporary fallback** for development and recovery while Ethernet+DHCP
  stability is not yet confirmed end-to-end. Tracked for removal in backlog
  [FOTA-REMOVE-UART-MCUMGR].

A/B slot swap uses MCUboot's **SWAP_MOVE** mode (no scratch partition required). The
new image must call `boot_write_img_confirmed()` before the next reset to become
permanent. The application waits a 5-second settle delay before confirming; if it
crashes or hangs before that call, MCUboot reverts to the previous slot on the next
reset.

---

## Rationale

### Why MCUboot without TF-M

The `frdm_mcxn947_mcxn947_cpu0_ns` TF-M target restricts Zephyr to the non-secure
RAM region (128 KB) defined by the default SAU layout. The gateway stack — HTTP
dashboard, MQTT, zbus, shell, sensor registry — requires approximately 320 KB. Extending
the NS RAM to cover additional SRAM banks (`sramg`, `sramh`) would require patching
the TF-M platform's SAU configuration, adding significant complexity for a home project.

MCUboot standalone gives the full 320 KB SRAM to the application and 984 KB flash per
image slot (vs. 256 KB in the TF-M layout), leaving ample room for future feature growth.

### Why ED25519

Compact 64-byte signature. Fast verification on Cortex-M33. Natively supported by
`imgtool` and `CONFIG_BOOT_SIGNATURE_TYPE_ED25519`. Smaller code footprint than RSA.

### Why SWAP_MOVE

SWAP_MOVE performs the slot exchange in-place without requiring a dedicated scratch
partition, making full use of the 2 MB internal flash. The two 984 KB image slots leave
no wasted space.

### Why the HTTP transport is in `lib/http_dashboard`

Consistent with ADR-011 and ADR-008: all gateway-facing services are exposed through
the existing authenticated HTTP surface. Adding a new route pair keeps the update UI
co-located with the config UI and reuses the existing session/bearer auth layer without
introducing a second network port or service.

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

### Positive

- Reliable updates with automatic rollback protect the device from bad firmware.
- UART transport provides an unconditional recovery path without network access.
- HTTP transport integrates naturally with the existing dashboard and auth layer.
- Sysbuild produces MCUboot + signed app in one `west build` invocation.
- 984 KB image slots leave ample room for gateway feature growth.
- Key generation script ensures no keys are ever committed to the repo.

### Negative / Constraints

- MCUboot adds ~80 KB to the flash layout; the application partition shrinks accordingly.
- The application must actively confirm each update; a confirm bug causes repeated
  rollback even on a correct image.
- Private key management is the operator's responsibility; loss of the private key
  means no further authenticated updates (re-flashing MCUboot with a new key required).
- native_sim target is not affected — MCUboot and FOTA are hardware-only features.
- MCUmgr over UART requires `CONFIG_HEAP_MEM_POOL_SIZE=16384` on the Cortex-M33
  build (SMP transport internals allocate from the system heap). This overhead will
  be eliminated once the UART transport is removed (backlog: [FOTA-REMOVE-UART-MCUMGR]).

---

## Related

- [ADR-008](ADR-008-kconfig-app-composition.md) — Kconfig composition model
- [ADR-011](ADR-011-http-dashboard.md) — HTTP dashboard (transport B home)
- [ADR-006](ADR-006-lora-channel-boundary.md) — LoRa (sensor-node relay, deferred)
- [`docs/architecture/firmware-update.md`](../architecture/firmware-update.md) — implementation detail, flash layout, diagrams
