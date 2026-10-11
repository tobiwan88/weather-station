# Phase 5: sub-sensor nodes end to end, gateway hardening

Part of [2026-10-11-modernization-round.md](2026-10-11-modernization-round.md). Planned by a Sonnet
planner against the v4.5.0-rc1 tree. Subtasks go to implementer agents (Haiku unless marked
Sonnet), reviewed by the `reviewer` agent.

## Decisions

- **One LoRa codec** in a new `lib/lora_proto`, used by node and gateway. 10-byte header
  (`seq_num[4]`, `LORA_FLAG_DIR_DOWN`), 12-byte GCM tag. With CRC the overhead is 24 bytes, so an
  SF12 packet (51 bytes) carries exactly 5 readings.
- **Nonce** = `node_id | direction | 32-bit counter`. The counter is persisted in settings with
  write-ahead reservation (64 values per flash write). The gateway keeps `last_rx` per node in RAM
  and drops `counter <= last_rx` (replay protection and dedup). The first frame after a gateway
  reboot is accepted.
- **Pre-shared per-node key, no over-the-air pairing.** `lora provision <id>` on the gateway
  generates and prints a key; `lora_node provision <id> <hex>` stores it on the node. A dev-only
  `LORA_NODE_DEV_KEY` covers native_sim. The old pairing (dev private key in source, session key
  sent in clear, no signature check on the node) is deleted. Authenticated OTA key exchange would
  need a gateway identity key and more flash than it is worth here.
- Encryption is required by default (`LORA_RADIO_REQUIRE_ENCRYPTION=y`). The node never sends
  plaintext unless `LORA_NODE_ALLOW_PLAINTEXT=y`.

## Wave 1 (parallel)

- [x] **S1** lora_e5_mini `&rng` enabled; hardware sensor UID fallbacks moved out of the fake
      range (BME680 0x0101, SEN0460 0x0111)
- [x] **S2** native_sim e2e: non-blocking FIFO open in `pipe_transport`; enable it on the
      gateway and the integration DUT; CI builds sensor-node and sets `SENSOR_NODE_EXE`
- [x] **S3** node apps enable `CONFIG_CMD` (a `select` caused a Kconfig loop) and the TX listeners
      are attached (nodes never transmitted); send batches in
      ≤ 5-reading chunks and requeue unsent readings; force-TX via work item
- [x] **S4** `uart_lora_sender`: listener + k_msgq instead of re-reading the channel (msg
      subscribers rejected: heap-backed net_buf per publish)
- [x] **S5** `remote_sensor_manager`: k_msgq instead of a single pending slot and a mutex in the
      listener

## Wave 2

- [x] **S6** (Sonnet) `lib/lora_proto` shared codec + tests (tampering, wrong key, size limits)
- [x] **S9** dashboard: forced password change on first boot (off for the integration DUT),
      login lockout with backoff and `429 Retry-After`

## Wave 3 (after S6)

- [x] **S7** (Sonnet) node: persisted counter, radio `tx=true`, no plaintext by default
- [x] **S10** (Sonnet) gateway: persisted TX counter, replay/dedup, require encryption,
      frequency/power from Kconfig, `lora_radio_send()` toggling TX config; archived-branch fixes

## Wave 4

- [x] **S8** (Sonnet) pre-shared key provisioning shells; delete OTA pairing
- [x] **S11** FOTA/RPC: real sequence numbers (every chunk reused one nonce), ACK matching,
      immediate completion on ACK; window size 1 documented

## Done when

`scripts/build-gate.sh` passes with the node→gateway integration tests running (not skipped),
and frdm_mcxn947 (sysbuild), lora_e5_mini and wio_e5_mini build.
