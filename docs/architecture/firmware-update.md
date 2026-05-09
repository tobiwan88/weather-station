# Firmware Update Architecture

This document covers the implementation detail for MCUboot-based FOTA on the
`frdm_mcxn947` gateway. For the architectural decision and rationale see
[ADR-014](../adr/ADR-014-mcuboot-fota.md).

---

## Boot chain

```
┌──────────────────────────────────────────────────────────────┐
│ NXP ROM Bootloader  (immutable, always present)              │
│   Loads MCUboot from flash 0x00000000                        │
└───────────────────────┬──────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────┐
│ MCUboot  (80 KB, 0x00000000)                                 │
│   1. Checks slot 1 for a pending image                       │
│   2. Verifies ED25519 signature                              │
│   3. If valid pending: SWAP_MOVE slot 1 → slot 0            │
│   4. Boots slot 0                                            │
└───────────────────────┬──────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────┐
│ Zephyr gateway application  (slot 0, up to 984 KB)           │
│   All libs self-wire via SYS_INIT.                           │
│   MCUmgr provides UART + HTTP FOTA transports.               │
│   SYS_INIT APPLICATION 99: confirm image after 5 s settle.  │
└──────────────────────────────────────────────────────────────┘
```

---

## Flash partition layout

Internal flash: 2 MB total. All sectors are 8 KB; all partition boundaries are
8 KB-aligned.

```
0x00000000 ┌────────────────────────────┐
           │  MCUboot                   │  80 KB   label: mcuboot
0x00014000 ├────────────────────────────┤
           │  Slot 0 — active image     │  984 KB  label: image-0
0x0010A000 ├────────────────────────────┤
           │  Slot 1 — update candidate │  984 KB  label: image-1
0x00200000 └────────────────────────────┘  (end of 2 MB internal flash)

External W25Q64 SPI flash (8 MB):
0x90000000 ┌────────────────────────────┐
           │  storage_partition          │  8 MB   Zephyr settings / NVS
0x90800000 └────────────────────────────┘
```

No scratch partition. SWAP_MOVE performs the A/B exchange using free sectors at
the top of each slot.

---

## RAM layout

Target: `frdm_mcxn947/mcxn947/cpu0` (standard variant, no TrustZone split).

| Region | Address | Size | Owner |
|--------|---------|------|-------|
| `sram0` | `0x20000000` | 320 KB | Zephyr application |
| `sramg` | `0x20050000` | 64 KB | CPU1 (reserved, do not use) |
| `sramh` | `0x20060000` | 32 KB | CPU1 (reserved, do not use) |

---

## Sysbuild configuration

Zephyr sysbuild builds MCUboot and the gateway app together in one invocation.

New files required:

```
apps/gateway/
  sysbuild.conf                    ← enables MCUboot for hardware builds
  sysbuild/mcuboot.conf            ← MCUboot Kconfig for this app
```

**`apps/gateway/sysbuild.conf`** (hardware targets only; excluded from native_sim via
board-specific Kconfig guards):

```ini
SB_CONFIG_BOOTLOADER_MCUBOOT=y
```

**`apps/gateway/sysbuild/mcuboot.conf`**:

```ini
CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y
CONFIG_BOOT_SWAP_USING_MOVE=y
CONFIG_BOOT_UPGRADE_ONLY=n          # rollback allowed
CONFIG_BOOT_BOOTSTRAP=n
CONFIG_BOOT_MAX_IMG_SECTORS=256
```

**Build command** (hardware):

```bash
./scripts/gen-dev-key.sh   # first time only

west build apps/gateway \
  -b frdm_mcxn947/mcxn947/cpu0 \
  --sysbuild \
  -- -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE=\"keys/dev-ed25519.pem\"
```

**Output artifacts**:

| File | Use |
|------|-----|
| `build/mcuboot/zephyr/zephyr.hex` | Flash once at device provisioning |
| `build/gateway/zephyr/zephyr.signed.bin` | FOTA payload for all subsequent updates |

---

## Key management

```
scripts/gen-dev-key.sh   ← committed; generates the key, never the key itself
keys/                    ← .gitignore'd; all key material lives here
  dev-ed25519.pem        ← generated locally, never committed
```

CI behaviour when `MCUBOOT_SIGN_KEY` secret is not set:

```bash
if [ -z "$MCUBOOT_SIGN_KEY" ]; then
  ./scripts/gen-dev-key.sh        # ephemeral key, discarded after run
else
  echo "$MCUBOOT_SIGN_KEY" > keys/dev-ed25519.pem
fi
```

A CI run without the secret verifies the build compiles and produces a signed binary;
the resulting artifact is not deployable. A CI run with the secret produces a
release-quality signed binary.

---

## MCUmgr — UART transport

UART already declared in the board DTS (`frdm_mcxn947_mcxn947_cpu0.dtsi`):

```dts
chosen {
    zephyr,uart-mcumgr = &flexcomm4_lpuart4;  /* same as console/shell */
};
```

Kconfig additions to `apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf`:

```ini
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_UART=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_IMG_MANAGER=y
CONFIG_STREAM_FLASH=y
CONFIG_FLASH_MAP=y
```

MCUmgr SMP and the shell share `flexcomm4_lpuart4`. They are distinguished by the
SMP frame start sequence (`0x06 0x09`) — no conflict.

Developer workflow (UART path):

```bash
mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 \
  image upload build/gateway/zephyr/zephyr.signed.bin
mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 \
  image test <hash>
mcumgr --conntype serial --connstring /dev/ttyACM0,baud=115200 reset
```

---

## MCUmgr — HTTP transport

Three new routes added to `lib/http_dashboard`:

| Method | Path | Auth | Action |
|--------|------|------|--------|
| `POST` | `/api/fota/upload` | session or bearer | Stream image to flash slot 1 |
| `POST` | `/api/fota/apply` | session or bearer | Mark slot 1 pending + reboot |
| `GET` | `/api/fota/status` | session or bearer | Return slot versions and state |

### Upload handler — streaming write

The handler never buffers the full image in RAM. It uses `flash_img_buffered_write()`
with a 4 KB write buffer to stream the HTTP body directly to slot 1:

```
HTTP POST /api/fota/upload  (Content-Type: application/octet-stream)
  → flash_img_init(&ctx, slot1_id)
  → for each received chunk:
       flash_img_buffered_write(&ctx, chunk, len, last)
  → on completion: return 200 + SHA256 hash of written image
```

`CONFIG_IMG_ERASE_PROGRESSIVELY=y` erases sectors just-in-time during streaming so
no separate erase step is needed before upload.

### Apply handler

```
POST /api/fota/apply
  → boot_request_upgrade(BOOT_UPGRADE_TEST)
  → k_work_schedule(&reboot_work, K_MSEC(2000))   ← allows HTTP response to flush
```

### Status response

```json
{
  "slot0": { "version": "1.2.0", "confirmed": true  },
  "slot1": { "version": "1.3.0", "confirmed": false },
  "pending_reboot": true
}
```

---

## Image confirmation

Registered via `SYS_INIT(fota_confirm_init, APPLICATION, 99)` — after all other
libraries have initialised.

```c
static void confirm_work_fn(struct k_work *work)
{
    /* Basic health: if we reached here, core services are up */
    boot_write_img_confirmed();
    LOG_INF("firmware update confirmed");
}
static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_work_fn);

static int fota_confirm_init(void)
{
    k_work_schedule(&confirm_work, K_SECONDS(5));
    return 0;
}
SYS_INIT(fota_confirm_init, APPLICATION, 99);
```

The 5-second delay ensures the HTTP dashboard, MQTT, and SNTP have had time to
bind their sockets. A binary that crashes or hangs before `boot_write_img_confirmed()`
is called will not be confirmed; MCUboot will revert to slot 0 on the next reset.

---

## End-to-end update flow

```
┌─────────────┐   POST /api/fota/upload        ┌───────────────────┐
│  Developer  │ ──────────────────────────────► │  HTTP dashboard   │
│  browser /  │                                 │  lib/http_dashboard│
│  curl       │   POST /api/fota/apply          │                   │
│             │ ──────────────────────────────► │  boot_request_    │
└─────────────┘                                 │  upgrade(TEST)    │
                                                └────────┬──────────┘
                                                         │ reboot
                                                ┌────────▼──────────┐
                                                │     MCUboot       │
                                                │  verify ED25519   │
                                                │  SWAP_MOVE        │
                                                └────────┬──────────┘
                                                         │ boot slot 0
                                                ┌────────▼──────────┐
                                                │  Zephyr (new)     │
                                                │  5 s settle       │
                                                │  confirmed ✓      │
                                                └───────────────────┘

Rollback path (if not confirmed):
  next reset → MCUboot detects unconfirmed slot 0
             → SWAP_MOVE reverses → previous firmware restored
```

---

## Sensor-node relay (stub — not implemented)

The gateway will eventually relay firmware to remote sensor nodes (e.g., STM32WLE5JC
via LoRa). The interface is defined here as a placeholder; implementation is deferred
to a future ADR (depends on ADR-006 LoRa implementation).

```c
/**
 * Transport vtable for gateway-to-sensor-node firmware relay.
 * Implement one instance per wireless transport (LoRa, BLE, …).
 */
struct fota_relay_transport_api {
    /**
     * Upload a firmware chunk to a target sensor node.
     * Caller chunks the image; transport handles fragmentation.
     */
    int (*upload)(uint32_t target_uid,
                  const uint8_t *data, size_t len,
                  size_t offset, bool last);

    /** Query the running firmware version on a target node. */
    int (*get_version)(uint32_t target_uid, char *buf, size_t len);
};
```

Key design constraints for the LoRa relay (to be resolved in the future ADR):

- STM32WLE5JC (Cortex-M4, no TrustZone) → MCUboot + ED25519, software keys only
- LoRa EU868 duty cycle (~1%) limits throughput; a 128 KB image takes hours
- Image must be stored on the gateway's external W25Q64 SPI flash during relay
- Chunking, ACK, and retry protocol are out of scope here

---

## Kconfig additions summary

`apps/gateway/boards/frdm_mcxn947_mcxn947_cpu0.conf` additions:

```ini
# MCUmgr + image management
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_UART=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y
CONFIG_IMG_MANAGER=y
CONFIG_STREAM_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y

# Increase poll slots for MCUmgr + HTTP coexistence
CONFIG_ZVFS_POLL_MAX=12
```

`prj.conf` is **not modified** — MCUmgr is hardware-only and must not affect the
`native_sim` build.

---

## Future hardening (out of scope)

| Item | What it requires |
|------|-----------------|
| Anti-rollback NV counter | `CONFIG_BOOT_ROLLBACK_PROT=y` + OTP/NV counter driver |
| Hardware key isolation | Migrate to TF-M + ITS; revisit RAM layout with custom SAU config |
| Encrypted image slots | MCUboot image encryption (`CONFIG_BOOT_ENCRYPT_IMAGE`) |
| CRA compliance | All three items above + attestation + vulnerability disclosure process |
