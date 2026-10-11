# Modernization round — 2026-10-11

Source: parallel repo audit (8 agents) on 2026-10-11, key findings verified by hand.
Execution model: Haiku subagents for small, file-scoped implementation tasks;
Sonnet subagents for reviews and per-phase subtask planning. One branch + PR per phase,
merged on green CI before the next phase starts.

Local build env: Apple `container` running `ghcr.io/tobiwan88/zephyr_docker:arm`
(SDK 1.0.1) with the CI extras (`pkgconf libsdl2-dev protobuf-compiler`, `pre-commit gcovr nanopb`).

## Phase 0 — Close and prune branches
- [x] Merge PR #50 (`feat/io-stream-abstraction`, CI green)
- [x] Archive-tag branches with unique work before deletion (local tags `archive/*`):
      `feat-lora-radio-check` (LoRa GCM/FOTA fixes → input to Phase 5),
      `feat/test-coverage-and-gcovr` (sensor_registry / location_registry tests → Phase 2)
- [x] Delete stale branches (local done; origin deletions pending owner approval): `add-secureboot`, `mini-changes` (superseded on master),
      `fix/benchmark-dorny-and-backlog-cleanup` (hardware docs superseded 2026-05-16),
      the two archived branches, and all branches already merged into master (local + origin)
- [x] Prune dead worktrees under `.worktrees/`

## Phase 1 — Modernize agent setup (CLAUDE.md, skills, hooks)
- [x] Fresh research: Claude Code recommendations Sep/Oct 2026
- [x] CLAUDE.md: < 120 lines, only facts needed every session, fix stale counts/paths,
      move area detail into path-scoped `.claude/rules/*.md`
- [x] Hooks (deterministic enforcement instead of prose): PreToolUse guard for banned
      APIs / `git add .`; PostToolUse format hook that works in the container
- [x] Merge overlapping skills, sharpen descriptions, delete dead references (`/sca`)
- [x] `.claude/agents/` definitions (haiku explorer, sonnet reviewer)
- [x] Untrack `.claude/settings.local.json`, shared `settings.json` with a tight allowlist;
      gitignore `.cocoindex_code/`

## Phase 2 — Fix confirmed bugs
- [ ] `sntp_sync.c:65` signed `(long)` fraction cast → negative tv_nsec on 32-bit
- [ ] `fake_sensors_timer.c:34` zbus publish from k_timer ISR → work item
- [ ] SEN0460 Q31 overflow for > 1000 µg/m³ → clamp
- [ ] `sensor_registry` returns pointers into locked table → copy-out API
- [ ] `/api/data` fixed 8 KB buffer overflow; `GET /api/config` leaks api_token
- [ ] `fota_confirm` confirms without health check
- [ ] Restore salvaged sensor_registry / location_registry tests
- [ ] `sensor_event_log` prints raw `%7.2f` (no FP log formatting); uid width inconsistent
- [ ] trace_recorder opt-in `k_malloc` dump path; prints under spinlock
- [ ] lvgl_display calls `lv_async_call` outside the LVGL lock from a zbus listener

## Phase 3 — Simplify ADRs and rules
- [ ] Collapse 17 ADRs into a short "decisions" doc keeping only rules that still constrain
      code; drop process ceremony (frozen/supersede workflow, auto-generated constraint table)
- [ ] Remove done/abandoned plans and specs; trim backlog to open items
- [ ] Drop skills that only exist to maintain removed process (`adr`, `arch-sync`, …)

## Phase 4 — Migrate to Zephyr v4.5.0-rc1
- [ ] Bump `west.yml`, fix breakages, both apps + twister green
- [ ] Audit self-written code vs. upstream equivalents (zbus async listeners, LoRa APIs,
      SNTP, settings, sensor RTIO, HTTP server, io_stream vs. upstream stream/flash APIs)
      and replace where upstream is better

## Phase 5 — Sub-sensor end-to-end + gateway hardening
- [ ] native_sim: enable `PIPE_TRANSPORT` on gateway, make node→gateway test non-skipping
- [ ] LoRa batching: split batches to ≤ 5 readings, requeue on failure
- [ ] Single LoRa packet codec shared by node and gateway, unit-tested
- [ ] Crypto: one tag length, reboot-safe nonce, reject plaintext from unpaired nodes
- [ ] Pairing on the node side; dev key out of source
- [ ] ACK-request + seq dedup; FOTA seq/window fixes
- [ ] Dashboard: force password change on first boot, login throttling
- [ ] Hardware sensor UID fallbacks (BME680 0x0011, SEN0460 0x0021) overlap the fake range 0x0001–0x00FF
