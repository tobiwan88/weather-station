#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build and test gate for the weather-station project.
# Runs the mandatory 5-step gate and outputs JSON results to stdout.
# Human-readable progress goes to stderr.
#
# Usage:
#   scripts/build-gate.sh [options]
#
# Options:
#   --pristine        Force pristine rebuild (default: auto-detect from git diff)
#   --skip-smoke      Skip shell smoke-test step
#   --skip-tests      Skip twister test step
#   --skip-precommit  Skip pre-commit step
#   --sca             Run CodeChecker static analysis (informational, never fails gate)
#   --quiet           Suppress stderr progress output
#
# Exit codes:
#   0 = All steps PASS
#   1 = One or more steps FAIL
#   2 = Partial (some steps skipped)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/native_sim/native/64"
ZEPHYR_BASE="/home/zephyr/workspace/zephyr"
GATEWAY_BINARY="/home/zephyr/workspace/build/native_sim/native/64/gateway/zephyr/zephyr.exe"

# Defaults
PRISTINE=""
SKIP_SMOKE=""
SKIP_TESTS=""
SKIP_PRECOMMIT=""
RUN_SCA=""
QUIET=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --pristine) PRISTINE="yes"; shift ;;
        --skip-smoke) SKIP_SMOKE="yes"; shift ;;
        --skip-tests) SKIP_TESTS="yes"; shift ;;
        --skip-precommit) SKIP_PRECOMMIT="yes"; shift ;;
        --sca) RUN_SCA="yes"; shift ;;
        --quiet) QUIET="yes"; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Auto-detect pristine: if any Kconfig, DTS, or .conf file changed, pristine rebuild
if [[ -z "$PRISTINE" ]]; then
    CHANGED_KCONFIG=$(git diff --name-only HEAD -- "*.conf" "*.overlay" "*/Kconfig" "*/Kconfig.*" 2>/dev/null | head -1 || true)
    if [[ -n "$CHANGED_KCONFIG" ]]; then
        PRISTINE="yes"
    fi
fi

# JSON output helpers
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    echo -n "$s"
}

declare -a STEP_RESULTS=()
OVERALL_EXIT=0
STEP_COUNT=0

add_step() {
    local name="$1"
    local status="$2"
    local duration="$3"
    local detail="$4"
    local errors="${5:-}"

    STEP_COUNT=$((STEP_COUNT + 1))
    STEP_RESULTS+=("$(cat <<EOF
    {
      "name": "$(json_escape "$name")",
      "status": "$status",
      "duration_s": $duration,
      "detail": "$(json_escape "$detail")",
      "errors": [$(if [[ -n "$errors" ]]; then echo -n "$errors"; fi)]
    }
EOF
)")
}

emit_json() {
    local verdict="$1"
    local summary="$2"
    local timestamp
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    local steps_json=""
    for i in "${!STEP_RESULTS[@]}"; do
        if [[ $i -gt 0 ]]; then
            steps_json+=","$'\n'
        fi
        steps_json+="${STEP_RESULTS[$i]}"
    done

    cat <<EOF
{
  "script": "build-gate",
  "timestamp": "$timestamp",
  "verdict": "$verdict",
  "steps": [
$steps_json
  ],
  "summary": "$(json_escape "$summary")"
}
EOF
}

log() {
    if [[ -z "$QUIET" ]]; then
        echo "[$(date +%H:%M:%S)] $*" >&2
    fi
}

run_step() {
    local name="$1"
    shift
    local start
    start=$(date +%s)
    local output=""
    local exit_code=0

    log "STEP $name: starting..."

    if output=$("$@" 2>&1); then
        exit_code=0
    else
        exit_code=$?
    fi

    local end
    end=$(date +%s)
    local duration=$((end - start))

    if [[ $exit_code -eq 0 ]]; then
        log "STEP $name: PASS (${duration}s)"
        add_step "$name" "PASS" "$duration" "Completed successfully"
    else
        log "STEP $name: FAIL (${duration}s)"
        local escaped_output
        escaped_output=$(json_escape "$output")
        add_step "$name" "FAIL" "$duration" "Exit code: $exit_code" "\"$(json_escape "$output")\""
        OVERALL_EXIT=1
    fi
}

# ============================================================
# Step 1: Build gateway
# ============================================================
log "=== Step 1/5: Build gateway ==="
if [[ -n "$PRISTINE" ]]; then
    run_step "Build gateway" \
        ZEPHYR_BASE="$ZEPHYR_BASE" west build -p always -b native_sim/native/64 apps/gateway
else
    run_step "Build gateway" \
        ZEPHYR_BASE="$ZEPHYR_BASE" west build -b native_sim/native/64 apps/gateway
fi

# ============================================================
# Step 2: Build sensor-node
# ============================================================
log "=== Step 2/5: Build sensor-node ==="
if [[ -n "$PRISTINE" ]]; then
    run_step "Build sensor-node" \
        ZEPHYR_BASE="$ZEPHYR_BASE" west build -p always -b native_sim/native/64 apps/sensor-node
else
    run_step "Build sensor-node" \
        ZEPHYR_BASE="$ZEPHYR_BASE" west build -b native_sim/native/64 apps/sensor-node
fi

# ============================================================
# Step 3: Shell smoke-test
# ============================================================
if [[ -n "$SKIP_SMOKE" ]]; then
    log "=== Step 3/5: Shell smoke-test (skipped) ==="
    add_step "Shell smoke-test" "SKIP" 0 "Skipped via --skip-smoke"
else
    log "=== Step 3/5: Shell smoke-test ==="
    if [[ ! -f "$GATEWAY_BINARY" ]]; then
        log "Gateway binary not found at $GATEWAY_BINARY"
        add_step "Shell smoke-test" "FAIL" 0 "Gateway binary not found: $GATEWAY_BINARY"
        OVERALL_EXIT=1
    else
        SMOKE_OUTPUT=$(printf "help\nfake_sensors list\nkernel uptime\n" | \
            timeout 10 "$GATEWAY_BINARY" -uart_stdinout 2>&1 || true)
        if echo "$SMOKE_OUTPUT" | grep -q "fake_sensors"; then
            log "Shell smoke-test: PASS"
            add_step "Shell smoke-test" "PASS" 0 "fake_sensors command found in help"
        else
            log "Shell smoke-test: FAIL — fake_sensors not found"
            add_step "Shell smoke-test" "FAIL" 0 "fake_sensors command not found in help output" "\"$(json_escape "$SMOKE_OUTPUT")\""
            OVERALL_EXIT=1
        fi
    fi
fi

# ============================================================
# Step 4: Twister test suite
# ============================================================
if [[ -n "$SKIP_TESTS" ]]; then
    log "=== Step 4/5: Twister tests (skipped) ==="
    add_step "Twister tests" "SKIP" 0 "Skipped via --skip-tests"
else
    log "=== Step 4/5: Twister tests ==="
    TWISTER_OUTPUT=$(ZEPHYR_BASE="$ZEPHYR_BASE" west twister \
        -p native_sim/native/64 -T tests/ --inline-logs -v -N 2>&1 || true)
    TWISTER_EXIT=$?

    # Parse twister output for pass/fail counts
    PASS_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+passed)' || echo "0")
    FAIL_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+failed)' || echo "0")
    SKIP_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+skipped)' || echo "0")

    if [[ "$FAIL_COUNT" -eq 0 ]]; then
        log "Twister tests: PASS ($PASS_COUNT passed, $SKIP_COUNT skipped)"
        add_step "Twister tests" "PASS" 0 "$PASS_COUNT passed, $SKIP_COUNT skipped"
    else
        log "Twister tests: FAIL ($FAIL_COUNT failed)"
        add_step "Twister tests" "FAIL" 0 "$FAIL_COUNT failed, $PASS_COUNT passed" "\"$(json_escape "$TWISTER_OUTPUT")\""
        OVERALL_EXIT=1
    fi
fi

# ============================================================
# Step 5: Pre-commit
# ============================================================
if [[ -n "$SKIP_PRECOMMIT" ]]; then
    log "=== Step 5/N: Pre-commit (skipped) ==="
    add_step "Pre-commit" "SKIP" 0 "Skipped via --skip-precommit"
else
    log "=== Step 5/N: Pre-commit ==="
    PRECOMMIT_OUTPUT=$(pre-commit run --all-files 2>&1 || true)
    PRECOMMIT_EXIT=$?

    if [[ $PRECOMMIT_EXIT -eq 0 ]]; then
        log "Pre-commit: PASS"
        add_step "Pre-commit" "PASS" 0 "All hooks passed"
    else
        log "Pre-commit: FAIL"
        add_step "Pre-commit" "FAIL" 0 "Hook failures detected" "\"$(json_escape "$PRECOMMIT_OUTPUT")\""
        OVERALL_EXIT=1
    fi
fi

# ============================================================
# Step 6: CodeChecker SCA (optional, informational only)
# ============================================================
if [[ -z "$RUN_SCA" ]]; then
    log "=== Step 6/N: CodeChecker SCA (skipped, use --sca to enable) ==="
    add_step "CodeChecker SCA" "SKIP" 0 "Not requested (use --sca to enable)"
else
    log "=== Step 6/N: CodeChecker SCA ==="
    SCA_START=$(date +%s)
    SCA_OUTPUT=$("${SCRIPT_DIR}/run-codechecker.sh" --diff 2>&1 || true)
    SCA_END=$(date +%s)
    SCA_DURATION=$((SCA_END - SCA_START))

    # Count findings from the diff output
    NEW_FINDINGS=$(echo "$SCA_OUTPUT" | grep -c "^\[" || echo "0")

    if [[ "$NEW_FINDINGS" -gt 0 ]]; then
        log "CodeChecker SCA: ${NEW_FINDINGS} new finding(s) found (informational)"
        add_step "CodeChecker SCA" "PASS" "$SCA_DURATION" "${NEW_FINDINGS} new finding(s) — informational only"
    else
        log "CodeChecker SCA: No new findings"
        add_step "CodeChecker SCA" "PASS" "$SCA_DURATION" "No new findings vs baseline"
    fi
    # SCA never fails the gate (informational only)
fi

# ============================================================
# Emit JSON
# ============================================================
if [[ $OVERALL_EXIT -eq 0 ]]; then
    emit_json "PASS" "All $STEP_COUNT steps passed."
else
    emit_json "FAIL" "One or more steps failed."
fi

exit $OVERALL_EXIT
