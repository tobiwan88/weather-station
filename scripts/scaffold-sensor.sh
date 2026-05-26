#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Scaffold a new fake sensor type for the weather-station project.
# Creates the Q31 helpers, DT binding, fake driver, and registers in overlays.
#
# Usage:
#   scripts/scaffold-sensor.sh <type_name> "<description>" <value_milli> <uid> <range_min> <range_max> <unit>
#
# Example:
#   scripts/scaffold-sensor.sh co2 "CO2 concentration" 412000 0x0003 400.0 5000.0 ppm
#
# Options:
#   --dry-run     Show what would be created without writing files
#   --skip-build  Skip the build verification step
#   --quiet       Suppress stderr progress output
#
# Exit codes:
#   0 = Scaffold created successfully
#   1 = Error (missing args, file exists, build failed)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ZEPHYR_BASE="/home/zephyr/workspace/zephyr"

# Parse arguments
DRY_RUN=""
SKIP_BUILD=""
QUIET=""
POSITIONAL=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) DRY_RUN="yes"; shift ;;
        --skip-build) SKIP_BUILD="yes"; shift ;;
        --quiet) QUIET="yes"; shift ;;
        *) POSITIONAL+=("$1"); shift ;;
    esac
done

if [[ ${#POSITIONAL[@]} -lt 7 ]]; then
    echo "Usage: $0 <type_name> \"<description>\" <value_milli> <uid> <range_min> <range_max> <unit>" >&2
    echo "Example: $0 co2 \"CO2 concentration\" 412000 0x0003 400.0 5000.0 ppm" >&2
    exit 1
fi

TYPE_NAME="${POSITIONAL[0]}"
DESCRIPTION="${POSITIONAL[1]}"
VALUE_MILLI="${POSITIONAL[2]}"
SENSOR_UID="${POSITIONAL[3]}"
RANGE_MIN="${POSITIONAL[4]}"
RANGE_MAX="${POSITIONAL[5]}"
UNIT="${POSITIONAL[6]}"

TYPE_NAME_UPPER=$(echo "$TYPE_NAME" | tr '[:lower:]' '[:upper:]' | tr '-' '_')

# Calculate span
RANGE_SPAN=$(python3 -c "print($RANGE_MAX - $RANGE_MIN)" 2>/dev/null || echo "0")

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

declare -a CREATED_FILES=()
declare -a MODIFIED_FILES=()

emit_json() {
    local verdict="$1"
    local summary="$2"
    local build_status="${3:-N/A}"
    local build_output="${4:-}"
    local timestamp
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    local created_json=""
    for i in "${!CREATED_FILES[@]}"; do
        [[ $i -gt 0 ]] && created_json+=","$'\n'
        created_json+="    \"$(json_escape "${CREATED_FILES[$i]}")\""
    done

    local modified_json=""
    for i in "${!MODIFIED_FILES[@]}"; do
        [[ $i -gt 0 ]] && modified_json+=","$'\n'
        modified_json+="    \"$(json_escape "${MODIFIED_FILES[$i]}")\""
    done

    cat <<EOF
{
  "script": "scaffold-sensor",
  "timestamp": "$timestamp",
  "verdict": "$verdict",
  "type_name": "$TYPE_NAME",
  "sensor_uid": "$SENSOR_UID",
  "q31_range": {
    "min": $RANGE_MIN,
    "max": $RANGE_MAX,
    "span": $RANGE_SPAN,
    "unit": "$UNIT"
  },
  "files_created": [
$created_json
  ],
  "files_modified": [
$modified_json
  ],
  "build_status": "$build_status",
  "build_output": "$(json_escape "$build_output")",
  "summary": "$(json_escape "$summary")"
}
EOF
}

# ============================================================
# Validate UID range
# ============================================================
UID_DEC=$((SENSOR_UID))
if [[ $UID_DEC -ge 1 && $UID_DEC -le 255 ]]; then
    log "UID $SENSOR_UID is in fake sensor range (0x0001-0x00FF)"
elif [[ $UID_DEC -ge 256 ]]; then
    log "UID $SENSOR_UID is in test/remote range"
else
    echo "{\"verdict\": \"FAIL\", \"error\": \"Invalid UID: $SENSOR_UID\"}" >&2
    exit 1
fi

# ============================================================
# Step 1: Add Q31 helpers to sensor_event.h
# ============================================================
SENSOR_EVENT_H="lib/sensor_event/include/sensor_event/sensor_event.h"
MODIFIED_FILES+=("$SENSOR_EVENT_H")

if [[ -z "$DRY_RUN" ]]; then
    # Check if helpers already exist
    if grep -q "${TYPE_NAME}_to_q31" "$REPO_ROOT/$SENSOR_EVENT_H" 2>/dev/null; then
        log "Q31 helpers already exist in $SENSOR_EVENT_H"
    else
        # Insert before the last #endif
        cat >> "$REPO_ROOT/$SENSOR_EVENT_H.tmp" <<HELPER_EOF

/** @brief Encode $DESCRIPTION to Q31. */
static inline int32_t ${TYPE_NAME}_to_q31(double value)
{
	return (int32_t)((value - ${RANGE_MIN}) / ${RANGE_SPAN} * (double)INT32_MAX);
}

/** @brief Decode Q31 to $DESCRIPTION. */
static inline double q31_to_${TYPE_NAME}(int32_t q31)
{
	return (double)q31 / (double)INT32_MAX * ${RANGE_SPAN} + ${RANGE_MIN};
}
HELPER_EOF
        # Insert before the final #endif
        sed -i '/^#endif.*SENSOR_EVENT_H_/r '"$REPO_ROOT/$SENSOR_EVENT_H.tmp" "$REPO_ROOT/$SENSOR_EVENT_H" 2>/dev/null || \
        cat "$REPO_ROOT/$SENSOR_EVENT_H.tmp" >> "$REPO_ROOT/$SENSOR_EVENT_H"
        rm -f "$REPO_ROOT/$SENSOR_EVENT_H.tmp"
        log "Added Q31 helpers to $SENSOR_EVENT_H"
    fi
else
    log "[DRY-RUN] Would add Q31 helpers to $SENSOR_EVENT_H"
fi

# ============================================================
# Step 2: Add fake_sensor_kind constant
# ============================================================
FAKE_SENSORS_H="lib/fake_sensors/include/fake_sensors/fake_sensors.h"
MODIFIED_FILES+=("$FAKE_SENSORS_H")

if [[ -z "$DRY_RUN" ]]; then
    if grep -q "FAKE_SENSOR_KIND_${TYPE_NAME_UPPER}" "$REPO_ROOT/$FAKE_SENSORS_H" 2>/dev/null; then
        log "Sensor kind already exists in $FAKE_SENSORS_H"
    else
        # Insert before the closing brace of the enum
        sed -i '/FAKE_SENSOR_KIND_HUMIDITY/a\\tFAKE_SENSOR_KIND_'${TYPE_NAME_UPPER}',' "$REPO_ROOT/$FAKE_SENSORS_H" 2>/dev/null || true
        log "Added FAKE_SENSOR_KIND_${TYPE_NAME_UPPER} to $FAKE_SENSORS_H"
    fi
else
    log "[DRY-RUN] Would add FAKE_SENSOR_KIND_${TYPE_NAME_UPPER} to $FAKE_SENSORS_H"
fi

# ============================================================
# Step 3: Create DT binding
# ============================================================
DT_BINDING="dts/bindings/fake,${TYPE_NAME}.yaml"
CREATED_FILES+=("$DT_BINDING")

if [[ -z "$DRY_RUN" ]]; then
    if [[ -f "$REPO_ROOT/$DT_BINDING" ]]; then
        log "DT binding already exists: $DT_BINDING"
    else
        cat > "$REPO_ROOT/$DT_BINDING" <<DT
description: Fake $TYPE_NAME sensor for native_sim testing

compatible: "fake,${TYPE_NAME}"

properties:
  sensor-uid:
    type: int
    required: true
    description: Unique sensor identifier (matches sensor_registry UID)

  location:
    type: string
    required: true
    description: Human-readable location label

  initial-value-m${UNIT}:
    type: int
    required: true
    description: Initial $DESCRIPTION in milli-$UNIT

status:
  required: false
  default: okay
DT
        log "Created $DT_BINDING"
    fi
else
    log "[DRY-RUN] Would create $DT_BINDING"
fi

# ============================================================
# Step 4: Create fake driver
# ============================================================
DRIVER_FILE="lib/fake_sensors/src/fake_${TYPE_NAME}.c"
CREATED_FILES+=("$DRIVER_FILE")

if [[ -z "$DRY_RUN" ]]; then
    if [[ -f "$REPO_ROOT/$DRIVER_FILE" ]]; then
        log "Driver already exists: $DRIVER_FILE"
    else
        # Copy from temperature template and replace tokens
        TEMPLATE="$REPO_ROOT/lib/fake_sensors/src/fake_temperature.c"
        if [[ -f "$TEMPLATE" ]]; then
            sed \
                -e "s/fake_temperature/fake_${TYPE_NAME}/g" \
                -e "s/fake,temperature/fake,${TYPE_NAME}/g" \
                -e "s/FAKE_SENSOR_KIND_TEMPERATURE/FAKE_SENSOR_KIND_${TYPE_NAME_UPPER}/g" \
                -e "s/SENSOR_TYPE_TEMPERATURE/SENSOR_TYPE_${TYPE_NAME_UPPER}/g" \
                -e "s/temperature_c_to_q31/${TYPE_NAME}_to_q31/g" \
                -e "s/fake_temp_mdegc/fake_${TYPE_NAME}_m${UNIT}/g" \
                -e "s/initial_value_mdegc/initial_value_m${UNIT}/g" \
                -e "s/SYS_INIT(fake_temperature_init, APPLICATION, 90)/SYS_INIT(fake_${TYPE_NAME}_init, APPLICATION, 91)/g" \
                -e "s/\"fake_temperature\"/\"fake_${TYPE_NAME}\"/g" \
                "$TEMPLATE" > "$REPO_ROOT/$DRIVER_FILE"
            log "Created $DRIVER_FILE (from template)"
        else
            echo "{\"verdict\": \"FAIL\", \"error\": \"Template not found: $TEMPLATE\"}" >&2
            exit 1
        fi
    fi
else
    log "[DRY-RUN] Would create $DRIVER_FILE"
fi

# ============================================================
# Step 5: Register in fake_sensors CMakeLists.txt
# ============================================================
FAKE_CMAKE="lib/fake_sensors/CMakeLists.txt"
MODIFIED_FILES+=("$FAKE_CMAKE")

if [[ -z "$DRY_RUN" ]]; then
    if ! grep -q "fake_${TYPE_NAME}.c" "$REPO_ROOT/$FAKE_CMAKE" 2>/dev/null; then
        sed -i "s|src/fake_humidity.c|src/fake_humidity.c src/fake_${TYPE_NAME}.c|g" "$REPO_ROOT/$FAKE_CMAKE"
        log "Registered driver in $FAKE_CMAKE"
    else
        log "Already registered in $FAKE_CMAKE"
    fi
else
    log "[DRY-RUN] Would register in $FAKE_CMAKE"
fi

# ============================================================
# Step 6: Add DT node to gateway overlay
# ============================================================
GATEWAY_OVERLAY="apps/gateway/boards/native_sim.overlay"
MODIFIED_FILES+=("$GATEWAY_OVERLAY")

if [[ -z "$DRY_RUN" ]]; then
    if ! grep -q "fake-${TYPE_NAME}" "$REPO_ROOT/$GATEWAY_OVERLAY" 2>/dev/null; then
        cat >> "$REPO_ROOT/$GATEWAY_OVERLAY" <<OVERLAY

	fake_${TYPE_NAME}_indoor: fake-${TYPE_NAME}-indoor {
		compatible = "fake,${TYPE_NAME}";
		sensor-uid = <${SENSOR_UID}>;
		location = "living_room";
		initial-value-m${UNIT} = <${VALUE_MILLI}>;
		status = "okay";
	};
OVERLAY
        log "Added DT node to $GATEWAY_OVERLAY"
    else
        log "DT node already exists in $GATEWAY_OVERLAY"
    fi
else
    log "[DRY-RUN] Would add DT node to $GATEWAY_OVERLAY"
fi

# ============================================================
# Build verification
# ============================================================
BUILD_STATUS="N/A"
BUILD_OUTPUT=""

if [[ -z "$DRY_RUN" && -z "$SKIP_BUILD" ]]; then
    log "Building gateway to verify scaffold..."
    BUILD_OUTPUT=$(ZEPHYR_BASE="$ZEPHYR_BASE" west build -p always -b native_sim/native/64 apps/gateway 2>&1 || true)
    if echo "$BUILD_OUTPUT" | grep -q "BUILD SUCCESS"; then
        BUILD_STATUS="PASS"
        log "Build: PASS"
    else
        BUILD_STATUS="FAIL"
        log "Build: FAIL"
    fi
elif [[ -n "$SKIP_BUILD" ]]; then
    BUILD_STATUS="SKIPPED"
    BUILD_OUTPUT="Skipped via --skip-build"
fi

# ============================================================
# Emit JSON
# ============================================================
if [[ "$BUILD_STATUS" == "FAIL" ]]; then
    emit_json "FAIL" "Scaffold created but build failed." "$BUILD_STATUS" "$BUILD_OUTPUT"
    exit 1
elif [[ -n "$DRY_RUN" ]]; then
    emit_json "DRY_RUN" "Would create ${#CREATED_FILES[@]} files, modify ${#MODIFIED_FILES[@]} files." "$BUILD_STATUS" "$BUILD_OUTPUT"
else
    emit_json "PASS" "Created ${#CREATED_FILES[@]} files, modified ${#MODIFIED_FILES[@]} files for fake_${TYPE_NAME}." "$BUILD_STATUS" "$BUILD_OUTPUT"
fi

exit 0
