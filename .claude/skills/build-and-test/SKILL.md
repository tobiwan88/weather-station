---
name: build-and-test
description: Use after any code, Kconfig, DTS, or config change — before committing. Runs the mandatory build + test gate for the weather-station project.
allowed-tools: Bash
disable-model-invocation: false
---

# Build and Test Gate

Run the mandatory build + test gate for the weather-station project.

## Use the script

Instead of running individual commands, use the build gate script:

```bash
scripts/build-gate.sh [options]
```

**Options:**
- `--pristine` — Force pristine rebuild (default: auto-detects from git diff)
- `--skip-smoke` — Skip shell smoke-test step
- `--skip-tests` — Skip twister test step
- `--skip-precommit` — Skip pre-commit step
- `--quiet` — Suppress stderr progress output

**Output:** JSON to stdout with per-step results, durations, and overall verdict. Human-readable progress goes to stderr.

**Example:**
```bash
scripts/build-gate.sh
scripts/build-gate.sh --pristine
scripts/build-gate.sh --skip-tests --skip-precommit
```

**JSON output schema:**
```json
{
  "script": "build-gate",
  "timestamp": "2026-05-25T12:00:00Z",
  "verdict": "PASS|FAIL",
  "steps": [
    {"name": "Build gateway", "status": "PASS|FAIL|SKIP", "duration_s": 12.3, "detail": "...", "errors": []},
    {"name": "Build sensor-node", "status": "PASS|FAIL|SKIP", "duration_s": 8.1, "detail": "...", "errors": []},
    {"name": "Shell smoke-test", "status": "PASS|FAIL|SKIP", "duration_s": 2.0, "detail": "...", "errors": []},
    {"name": "Twister tests", "status": "PASS|FAIL|SKIP", "duration_s": 45.0, "detail": "...", "errors": []},
    {"name": "Pre-commit", "status": "PASS|FAIL|SKIP", "duration_s": 3.0, "detail": "...", "errors": []}
  ],
  "summary": "All 5 steps passed."
}
```

**Exit codes:** 0 = all PASS, 1 = one or more FAIL, 2 = partial (some skipped)

## Manual steps (for reference)

The script runs these 5 steps in order:

1. **Build gateway** — `west build -b native_sim/native/64 apps/gateway`
2. **Build sensor-node** — `west build -b native_sim/native/64 apps/sensor-node`
3. **Shell smoke-test** — Pipe commands to gateway binary, verify `fake_sensors` appears
4. **Twister** — `west twister -p native_sim/native/64 -T tests/ --inline-logs -v -N`
5. **Pre-commit** — `pre-commit run --all-files`

Use `--pristine` (or `-p always`) after any Kconfig, DTS overlay, or `.conf` change.
The script auto-detects this from git diff.

## Gotchas

- **ZEPHYR_BASE is stale.** The script always uses `ZEPHYR_BASE=/home/zephyr/workspace/zephyr` for twister.
- **Pristine vs. incremental.** Kconfig/DTS/conf changes require `-p always`. The script auto-detects this.
- **Mosquitto.** MQTT tests skip silently if no broker is running. Start `mosquitto -p 1883 -d` for full coverage.
- **CMakeCache.txt.** If builds fail with a stale path, delete `CMakeCache.txt` and rebuild.

## Red Flags — STOP and re-run the failed step

| Feeling | Reality |
|---------|---------|
| "Should pass" / "probably fine" / "looks correct" | You didn't verify |
| Trusting a build from an earlier session | Not fresh evidence |
| Skipping sensor-node build because "only gateway changed" | Build both |
| Skipping shell smoke-test because "just a small change" | Small changes can break init |
| Proceeding past a test failure saying "will fix later" | Fix now or don't proceed |
| Proceeding past a pre-commit hook failure | Fix now or don't proceed |
| Any wording implying success without having RUN the verification command | No evidence = no claim |

## Binary path reference

```
/home/zephyr/workspace/build/native_sim/native/64/gateway/zephyr/zephyr.exe
```
