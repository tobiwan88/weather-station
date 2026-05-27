#!/usr/bin/env bash
# scripts/run-codechecker.sh
#
# Run CodeChecker static analysis on weather-station apps.
# Uses Zephyr's built-in CodeChecker integration (ZEPHYR_SCA_VARIANT=codechecker).
#
# Usage:
#   ./scripts/run-codechecker.sh              Run analysis, print summary
#   ./scripts/run-codechecker.sh --diff       Compare against baseline, show NEW findings
#   ./scripts/run-codechecker.sh --rebaseline Regenerate baseline from current findings
#   ./scripts/run-codechecker.sh --html [DIR] Generate static HTML report
#
# Baseline lifecycle:
#   1. Run --rebaseline to capture all current findings
#   2. Commit sca/codechecker.baseline to the repo
#   3. On each change, run --diff to see only NEW findings
#   4. When checkers/config change, run --rebaseline again

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEST_ROOT="$(west topdir 2>/dev/null || echo "${REPO_ROOT}/..")"
ZEPHYR_BASE="${WEST_ROOT}/zephyr"
BOARD="native_sim/native/64"
BASELINE_FILE="${REPO_ROOT}/sca/codechecker.baseline"
SKIP_FILE="${REPO_ROOT}/sca/codechecker.skip"

REPORT_DIR=$(mktemp -d /tmp/cc-reports-XXXXXX)
trap 'rm -rf "$REPORT_DIR"' EXIT

# Use context-free-v2 hash for stable differential comparison across runs
ANALYZER_OPTS="--report-hash;context-free-v2"
if [ -f "$SKIP_FILE" ]; then
  ANALYZER_OPTS="${ANALYZER_OPTS};--ignore;${SKIP_FILE}"
fi

# ── Helpers ─────────────────────────────────────────────────────────────────

check_prerequisites() {
  if ! command -v CodeChecker &>/dev/null && ! command -v codechecker &>/dev/null; then
    echo "ERROR: CodeChecker is not installed."
    echo "Install with: pip3 install codechecker"
    exit 1
  fi
}

build_app() {
  local app_name="$1"
  local build_dir="${REPO_ROOT}/build/native_sim_native_64/${app_name}"

  echo ""
  echo "=== Building ${app_name} with CodeChecker ==="

  ZEPHYR_BASE="${ZEPHYR_BASE}" \
  ZEPHYR_EXTRA_MODULES="${WEST_ROOT}/weather-station" \
    west build -p always -b "${BOARD}" \
      --build-dir "${build_dir}" \
      "${WEST_ROOT}/weather-station/apps/${app_name}" \
      -- \
      -DZEPHYR_SCA_VARIANT=codechecker \
      -DCODECHECKER_PARSE_SKIP=1 \
      -DCODECHECKER_ANALYZE_JOBS="$(nproc 2>/dev/null || echo 4)" \
      "-DCODECHECKER_ANALYZE_OPTS=${ANALYZER_OPTS}"

  local plist_dir="${build_dir}/sca/codechecker/codechecker.plist"
  if [ -d "$plist_dir" ] && [ "$(ls -A "$plist_dir" 2>/dev/null)" ]; then
    cp -r "${plist_dir:?}"/* "${REPORT_DIR}/"
    echo "  Copied ${app_name} analysis results"
  else
    echo "  WARNING: No analysis results for ${app_name}"
  fi
}

parse_summary() {
  echo ""
  echo "=== CodeChecker Summary ==="
  CodeChecker parse "${REPORT_DIR}" \
    --trim-path-prefix "${WEST_ROOT}" || true
}

diff_against_baseline() {
  if [ ! -f "$BASELINE_FILE" ]; then
    echo ""
    echo "WARNING: No baseline file found at ${BASELINE_FILE}"
    echo "Run with --rebaseline to create one."
    return
  fi

  echo ""
  echo "=== New Findings (vs baseline) ==="
  CodeChecker cmd diff \
    -b "$BASELINE_FILE" \
    -n "$REPORT_DIR" \
    --new \
    --print-steps 2>&1 || true
}

regenerate_baseline() {
  echo ""
  echo "=== Regenerating baseline ==="
  CodeChecker parse "${REPORT_DIR}" \
    --trim-path-prefix "${WEST_ROOT}" \
    -e baseline \
    -o "$BASELINE_FILE" || true
  echo "Baseline written to ${BASELINE_FILE}"
}

generate_html() {
  local html_dir="${1:-./reports_html}"
  echo ""
  echo "=== Generating HTML report → ${html_dir} ==="
  CodeChecker parse "${REPORT_DIR}" \
    --trim-path-prefix "${WEST_ROOT}" \
    --export html \
    --output "$html_dir" || true
  echo "Open ${html_dir}/index.html in a browser to view results."
}

# ── Main ────────────────────────────────────────────────────────────────────

check_prerequisites

case "${1:-}" in
  --diff)
    build_app gateway
    build_app sensor-node
    parse_summary
    diff_against_baseline
    ;;
  --rebaseline)
    build_app gateway
    build_app sensor-node
    parse_summary
    regenerate_baseline
    ;;
  --html)
    build_app gateway
    build_app sensor-node
    parse_summary
    generate_html "${2:-./reports_html}"
    ;;
  "")
    build_app gateway
    build_app sensor-node
    parse_summary
    ;;
  *)
    echo "Usage: $0 [--diff|--rebaseline|--html [DIR]]"
    exit 1
    ;;
esac
