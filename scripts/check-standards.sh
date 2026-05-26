#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Standards check for the weather-station project.
# Runs pre-commit gate and constraint scans on changed files.
# Outputs JSON results to stdout. Human-readable progress goes to stderr.
#
# Usage:
#   scripts/check-standards.sh [git-ref] [file...]
#
# Examples:
#   scripts/check-standards.sh                  # uncommitted changes
#   scripts/check-standards.sh HEAD~1..HEAD     # last commit
#   scripts/check-standards.sh master..HEAD     # all commits since master
#   scripts/check-standards.sh -- file1.c file2.h  # specific files
#
# Exit codes:
#   0 = PASS (no blocking issues)
#   1 = BLOCK (blocking issues found)
#   2 = WARN (warnings only, no blockers)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Parse arguments
GIT_REF=""
FILES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --) shift; FILES+=("$@"); break ;;
        *)
            if [[ "$1" == *..* || "$1" == *~* ]]; then
                GIT_REF="$1"
            else
                FILES+=("$1")
            fi
            shift
            ;;
    esac
done

# Resolve changed files
declare -a CHANGED_FILES=()
if [[ ${#FILES[@]} -gt 0 ]]; then
    CHANGED_FILES=("${FILES[@]}")
elif [[ -n "$GIT_REF" ]]; then
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only "$GIT_REF")
else
    # Uncommitted changes
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only HEAD 2>/dev/null || true)
    while IFS= read -r f; do
        [[ -n "$f" ]] && CHANGED_FILES+=("$f")
    done < <(git diff --name-only --cached 2>/dev/null || true)
    # Deduplicate
    if [[ ${#CHANGED_FILES[@]} -gt 0 ]]; then
        mapfile -t CHANGED_FILES < <(printf '%s\n' "${CHANGED_FILES[@]}" | sort -u)
    fi
fi

if [[ ${#CHANGED_FILES[@]} -eq 0 ]]; then
    cat <<'EOF'
{
  "script": "check-standards",
  "timestamp": "",
  "verdict": "PASS",
  "files_checked": [],
  "checks": [],
  "blocking": [],
  "warnings": [],
  "summary": "No changes to check."
}
EOF
    exit 0
fi

# JSON helpers
json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    echo -n "$s"
}

log() { echo "[$(date +%H:%M:%S)] $*" >&2; }

# ============================================================
# Step 1: Pre-commit gate
# ============================================================
log "Running pre-commit on ${#CHANGED_FILES[@]} files..."
PRECOMMIT_OUTPUT=""
PRECOMMIT_STATUS="PASS"
PRECOMMIT_ERRORS=()

PRECOMMIT_OUTPUT=$(pre-commit run --files "${CHANGED_FILES[@]}" 2>&1 || true)
if echo "$PRECOMMIT_OUTPUT" | grep -qE "^(Passed|Fixing)" || ! echo "$PRECOMMIT_OUTPUT" | grep -qE "^(Failed|failed)"; then
    PRECOMMIT_STATUS="PASS"
else
    PRECOMMIT_STATUS="FAIL"
    while IFS= read -r line; do
        PRECOMMIT_ERRORS+=("$line")
    done < <(echo "$PRECOMMIT_OUTPUT" | grep -E "^(Failed|failed)" || true)
fi

# ============================================================
# Step 2: Constraint scans
# ============================================================
declare -a CHECK_RESULTS=()
declare -a BLOCKING_ISSUES=()
declare -a WARNINGS=()

run_check() {
    local id="$1"
    local name="$2"
    local rule="$3"
    local severity="$4"  # BLOCKING or WARN
    local pattern="$5"
    shift 5
    local -a target_files=("$@")

    if [[ ${#target_files[@]} -eq 0 ]]; then
        CHECK_RESULTS+=("{\"id\": \"$id\", \"name\": \"$(json_escape "$name")\", \"rule\": \"$(json_escape "$rule")\", \"status\": \"N/A\", \"matches\": [], \"severity\": \"$severity\"}")
        return
    fi

    local output=""
    output=$(grep -nE "$pattern" "${target_files[@]}" 2>/dev/null || true)

    if [[ -z "$output" ]]; then
        CHECK_RESULTS+=("{\"id\": \"$id\", \"name\": \"$(json_escape "$name")\", \"rule\": \"$(json_escape "$rule")\", \"status\": \"COMPLIANT\", \"matches\": [], \"severity\": \"$severity\"}")
    else
        local -a matches=()
        while IFS= read -r line; do
            matches+=("\"$(json_escape "$line")\"")
        done < <(echo "$output")

        local matches_json
        matches_json=$(IFS=,; echo "${matches[*]}")

        CHECK_RESULTS+=("{\"id\": \"$id\", \"name\": \"$(json_escape "$name")\", \"rule\": \"$(json_escape "$rule")\", \"status\": \"VIOLATION\", \"matches\": [$matches_json], \"severity\": \"$severity\"}")

        if [[ "$severity" == "BLOCKING" ]]; then
            while IFS= read -r line; do
                BLOCKING_ISSUES+=("$line")
            done < <(echo "$output")
        else
            while IFS= read -r line; do
                WARNINGS+=("$line")
            done < <(echo "$output")
        fi
    fi
}

# Filter files by type for targeted checks
declare -a C_FILES=() H_FILES=() CMAKE_FILES=() KCONFIG_FILES=() CONF_FILES=() OVERLAY_FILES=() SRC_LIB_FILES=()

for f in "${CHANGED_FILES[@]}"; do
    case "$f" in
        *.c) C_FILES+=("$f") ;;
        *.h) H_FILES+=("$f") ;;
        *CMakeLists.txt) CMAKE_FILES+=("$f") ;;
        *Kconfig*) KCONFIG_FILES+=("$f") ;;
        *.conf) CONF_FILES+=("$f") ;;
        *.overlay) OVERLAY_FILES+=("$f") ;;
    esac
    # src/ and lib/ files for printk check
    if [[ "$f" == src/* || "$f" == lib/* ]]; then
        SRC_LIB_FILES+=("$f")
    fi
done

log "Running constraint scans..."

# 4.1 No heap allocations
run_check "heap" "No heap allocations" "ADR-002, ADR-003" "BLOCKING" \
    '\b(malloc|free|k_malloc|k_free|k_calloc)\b' \
    "${C_FILES[@]}" "${H_FILES[@]}" 2>/dev/null || true

# 4.2 zbus channel definition in headers
run_check "zbus-chan" "ZBUS_CHAN_DEFINE in headers" "ADR-002" "BLOCKING" \
    'ZBUS_CHAN_DEFINE' \
    "${H_FILES[@]}" 2>/dev/null || true

# 4.3 printk in non-test code
run_check "printk" "printk in non-test code" "CLAUDE.md" "BLOCKING" \
    '\bprintk\b' \
    "${SRC_LIB_FILES[@]}" 2>/dev/null || true

# 4.4 Bus API without _dt suffix
run_check "bus-api" "Bus API without _dt suffix" "CLAUDE.md" "BLOCKING" \
    '\b(i2c_write|i2c_read|i2c_burst_read|i2c_burst_write|spi_read|spi_write|spi_transceive)\s*\(' \
    "${C_FILES[@]}" 2>/dev/null | grep -v '_dt' || true
# Re-run properly: get matches, filter out _dt
BUS_API_OUTPUT=""
if [[ ${#C_FILES[@]} -gt 0 ]]; then
    BUS_API_OUTPUT=$(grep -nE '\b(i2c_write|i2c_read|i2c_burst_read|i2c_burst_write|spi_read|spi_write|spi_transceive)\s*\(' "${C_FILES[@]}" 2>/dev/null | grep -v '_dt' || true)
fi
if [[ -z "$BUS_API_OUTPUT" ]]; then
    CHECK_RESULTS+=("{\"id\": \"bus-api\", \"name\": \"Bus API without _dt suffix\", \"rule\": \"CLAUDE.md\", \"status\": \"COMPLIANT\", \"matches\": [], \"severity\": \"BLOCKING\"}")
else
    local_matches=()
    while IFS= read -r line; do
        local_matches+=("\"$(json_escape "$line")\"")
    done < <(echo "$BUS_API_OUTPUT")
    local_matches_json=$(IFS=,; echo "${local_matches[*]}")
    CHECK_RESULTS+=("{\"id\": \"bus-api\", \"name\": \"Bus API without _dt suffix\", \"rule\": \"CLAUDE.md\", \"status\": \"VIOLATION\", \"matches\": [$local_matches_json], \"severity\": \"BLOCKING\"}")
    BLOCKING_ISSUES+=("$BUS_API_OUTPUT")
fi

# 4.5 Unchecked zbus return values
ZBUS_PUB_OUTPUT=""
if [[ ${#C_FILES[@]} -gt 0 ]]; then
    ZBUS_PUB_OUTPUT=$(grep -nE '^\s*zbus_chan_pub\s*\(' "${C_FILES[@]}" 2>/dev/null || true)
fi
if [[ -z "$ZBUS_PUB_OUTPUT" ]]; then
    CHECK_RESULTS+=("{\"id\": \"zbus-rc\", \"name\": \"Unchecked zbus return values\", \"rule\": \"ADR-002\", \"status\": \"COMPLIANT\", \"matches\": [], \"severity\": \"WARN\"}")
else
    local_matches=()
    while IFS= read -r line; do
        local_matches+=("\"$(json_escape "$line")\"")
    done < <(echo "$ZBUS_PUB_OUTPUT")
    local_matches_json=$(IFS=,; echo "${local_matches[*]}")
    CHECK_RESULTS+=("{\"id\": \"zbus-rc\", \"name\": \"Unchecked zbus return values\", \"rule\": \"ADR-002\", \"status\": \"VIOLATION\", \"matches\": [$local_matches_json], \"severity\": \"WARN\"}")
    WARNINGS+=("$ZBUS_PUB_OUTPUT")
fi

# 4.6 Kconfig-only composition
run_check "kconfig-comp" "Kconfig-only composition" "ADR-008" "BLOCKING" \
    'target_link_libraries' \
    "${CMAKE_FILES[@]}" 2>/dev/null || true

# ============================================================
# Determine verdict
# ============================================================
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

if [[ "$PRECOMMIT_STATUS" == "FAIL" ]]; then
    VERDICT="BLOCK"
    SUMMARY="Pre-commit gate failed."
    EXIT_CODE=1
elif [[ ${#BLOCKING_ISSUES[@]} -gt 0 ]]; then
    VERDICT="BLOCK"
    SUMMARY="${#BLOCKING_ISSUES[@]} blocking issue(s) found."
    EXIT_CODE=1
elif [[ ${#WARNINGS[@]} -gt 0 ]]; then
    VERDICT="WARN"
    SUMMARY="${#WARNINGS[@]} warning(s) found, no blocking issues."
    EXIT_CODE=2
else
    VERDICT="PASS"
    SUMMARY="All checks passed."
    EXIT_CODE=0
fi

# ============================================================
# Emit JSON
# ============================================================
FILES_JSON=""
for i in "${!CHANGED_FILES[@]}"; do
    [[ $i -gt 0 ]] && FILES_JSON+=", "
    FILES_JSON+="\"$(json_escape "${CHANGED_FILES[$i]}")\""
done

CHECKS_JSON=""
for i in "${!CHECK_RESULTS[@]}"; do
    [[ $i -gt 0 ]] && CHECKS_JSON+=","$'\n'
    CHECKS_JSON+="    ${CHECK_RESULTS[$i]}"
done

BLOCKING_JSON=""
for i in "${!BLOCKING_ISSUES[@]}"; do
    [[ $i -gt 0 ]] && BLOCKING_JSON+=","$'\n'
    BLOCKING_JSON+="    \"$(json_escape "${BLOCKING_ISSUES[$i]}")\""
done

WARNINGS_JSON=""
for i in "${!WARNINGS[@]}"; do
    [[ $i -gt 0 ]] && WARNINGS_JSON+=","$'\n'
    WARNINGS_JSON+="    \"$(json_escape "${WARNINGS[$i]}")\""
done

cat <<EOF
{
  "script": "check-standards",
  "timestamp": "$TIMESTAMP",
  "verdict": "$VERDICT",
  "files_checked": [$FILES_JSON],
  "pre_commit": {
    "status": "$PRECOMMIT_STATUS",
    "output": "$(json_escape "$PRECOMMIT_OUTPUT")"
  },
  "checks": [
$CHECKS_JSON
  ],
  "blocking": [$BLOCKING_JSON],
  "warnings": [$WARNINGS_JSON],
  "summary": "$(json_escape "$SUMMARY")"
}
EOF

exit $EXIT_CODE
