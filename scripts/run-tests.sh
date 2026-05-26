#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Run integration tests for the weather-station project.
# Wraps west twister with pre-flight checks and structured JSON output.
#
# Usage:
#   scripts/run-tests.sh [options]
#
# Options:
#   --marker MARKER     Run tests matching marker (smoke|shell|http|mqtt|e2e|system)
#   --test NAME         Run a single test by name (substring match)
#   --all               Run all tests (unit + integration)
#   --skip-mosquitto    Skip mosquitto pre-flight (MQTT tests will be skipped)
#   --quiet             Suppress stderr progress output
#
# Exit codes:
#   0 = All tests PASS
#   1 = One or more tests FAIL
#   2 = Partial (some tests skipped)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR_BASE="/home/zephyr/workspace/zephyr"

# Defaults
MARKER=""
TEST_NAME=""
RUN_ALL=""
SKIP_MOSQUITTO=""
QUIET=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --marker) MARKER="$2"; shift 2 ;;
        --test) TEST_NAME="$2"; shift 2 ;;
        --all) RUN_ALL="yes"; shift ;;
        --skip-mosquitto) SKIP_MOSQUITTO="yes"; shift ;;
        --quiet) QUIET="yes"; shift ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

log() { if [[ -z "$QUIET" ]]; then echo "[$(date +%H:%M:%S)] $*" >&2; fi; }

json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    echo -n "$s"
}

# ============================================================
# Pre-flight: Mosquitto
# ============================================================
MQTT_STATUS="N/A"
MQTT_DETAIL="No MQTT tests requested"

if [[ -z "$SKIP_MOSQUITTO" ]]; then
    if [[ -n "$MARKER" && "$MARKER" == *"mqtt"* ]] || [[ -z "$MARKER" && -z "$TEST_NAME" ]]; then
        log "Pre-flight: Checking mosquitto..."
        if mosquitto -p 1883 -d 2>/dev/null; then
            sleep 0.5
        fi
        if ss -tlnp 2>/dev/null | grep -q ':1883' || netstat -tlnp 2>/dev/null | grep -q ':1883'; then
            MQTT_STATUS="RUNNING"
            MQTT_DETAIL="Mosquitto running on port 1883"
            log "Mosquitto: RUNNING"
        else
            MQTT_STATUS="NOT_RUNNING"
            MQTT_DETAIL="Mosquitto not available — MQTT tests will be skipped"
            log "Mosquitto: NOT_RUNNING (MQTT tests will skip)"
        fi
    fi
fi

# ============================================================
# Build twister command
# ============================================================
TEST_DIR="tests/integration"
PYTEST_ARGS=""

if [[ -n "$RUN_ALL" ]]; then
    TEST_DIR="tests/"
elif [[ -n "$MARKER" && -n "$TEST_NAME" ]]; then
    PYTEST_ARGS="--pytest-args=\"-m '$MARKER' -k $TEST_NAME\""
elif [[ -n "$MARKER" ]]; then
    PYTEST_ARGS="--pytest-args=\"-m $MARKER\""
elif [[ -n "$TEST_NAME" ]]; then
    PYTEST_ARGS="--pytest-args=\"-k $TEST_NAME\""
fi

log "Running twister on $TEST_DIR..."
if [[ -n "$PYTEST_ARGS" ]]; then
    log "Args: $PYTEST_ARGS"
fi

# Run twister
TWISTER_START=$(date +%s)
TWISTER_OUTPUT=""
TWISTER_EXIT=0

if [[ -n "$MARKER" ]]; then
    TWISTER_OUTPUT=$(ZEPHYR_BASE="$ZEPHYR_BASE" west twister \
        -p native_sim/native/64 \
        -T "$TEST_DIR" \
        --inline-logs -v -N \
        --pytest-args="-m $MARKER" \
        2>&1 || true)
elif [[ -n "$TEST_NAME" ]]; then
    TWISTER_OUTPUT=$(ZEPHYR_BASE="$ZEPHYR_BASE" west twister \
        -p native_sim/native/64 \
        -T "$TEST_DIR" \
        --inline-logs -v -N \
        --pytest-args="-k $TEST_NAME" \
        2>&1 || true)
else
    TWISTER_OUTPUT=$(ZEPHYR_BASE="$ZEPHYR_BASE" west twister \
        -p native_sim/native/64 \
        -T "$TEST_DIR" \
        --inline-logs -v -N \
        2>&1 || true)
fi
TWISTER_EXIT=$?

TWISTER_END=$(date +%s)
TWISTER_DURATION=$((TWISTER_END - TWISTER_START))

# Parse results
PASS_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+passed)' | tail -1 || echo "0")
FAIL_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+failed)' | tail -1 || echo "0")
SKIP_COUNT=$(echo "$TWISTER_OUTPUT" | grep -oP '\d+(?=\s+skipped)' | tail -1 || echo "0")

# Handle case where grep finds nothing
[[ -z "$PASS_COUNT" ]] && PASS_COUNT=0
[[ -z "$FAIL_COUNT" ]] && FAIL_COUNT=0
[[ -z "$SKIP_COUNT" ]] && SKIP_COUNT=0

TOTAL=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))

log "Results: $PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped (${TWISTER_DURATION}s)"

# Determine verdict
if [[ "$FAIL_COUNT" -eq 0 ]]; then
    VERDICT="PASS"
    EXIT_CODE=0
    if [[ "$SKIP_COUNT" -gt 0 ]]; then
        SUMMARY="$PASS_COUNT passed, $SKIP_COUNT skipped. No failures."
        EXIT_CODE=2
    else
        SUMMARY="All $PASS_COUNT tests passed."
    fi
else
    VERDICT="FAIL"
    EXIT_CODE=1
    SUMMARY="$FAIL_COUNT test(s) failed, $PASS_COUNT passed, $SKIP_COUNT skipped."
fi

# ============================================================
# Emit JSON
# ============================================================
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

cat <<EOF
{
  "script": "run-tests",
  "timestamp": "$TIMESTAMP",
  "verdict": "$VERDICT",
  "config": {
    "marker": "$(json_escape "${MARKER:-all}")",
    "test_filter": "$(json_escape "${TEST_NAME:-none}")",
    "test_dir": "$(json_escape "$TEST_DIR")",
    "mosquitto": "$MQTT_STATUS"
  },
  "results": {
    "passed": $PASS_COUNT,
    "failed": $FAIL_COUNT,
    "skipped": $SKIP_COUNT,
    "total": $TOTAL,
    "duration_s": $TWISTER_DURATION
  },
  "output": "$(json_escape "$TWISTER_OUTPUT")",
  "summary": "$(json_escape "$SUMMARY")"
}
EOF

exit $EXIT_CODE
