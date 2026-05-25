#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Scaffold a new library under lib/ for the weather-station project.
# Creates the directory structure, Kconfig, CMakeLists.txt, header, source,
# wires into the build system, and enables in app configs.
#
# Usage:
#   scripts/scaffold-lib.sh <lib_name> "<description>" <kconfig_symbol> <sys_init_priority>
#
# Example:
#   scripts/scaffold-lib.sh mqtt_publisher "MQTT telemetry publisher" MQTT_PUBLISHER 95
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

if [[ ${#POSITIONAL[@]} -lt 4 ]]; then
    echo "Usage: $0 <lib_name> \"<description>\" <kconfig_symbol> <sys_init_priority>" >&2
    echo "Example: $0 mqtt_publisher \"MQTT telemetry publisher\" MQTT_PUBLISHER 95" >&2
    exit 1
fi

LIB_NAME="${POSITIONAL[0]}"
DESCRIPTION="${POSITIONAL[1]}"
KCONFIG_SYMBOL="${POSITIONAL[2]}"
SYS_INIT_PRIORITY="${POSITIONAL[3]}"

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

# Validate
if [[ -d "$REPO_ROOT/lib/$LIB_NAME" ]]; then
    echo "{\"verdict\": \"FAIL\", \"error\": \"Library $LIB_NAME already exists at lib/$LIB_NAME\"}" >&2
    exit 1
fi

# Derive uppercase version for include guards
LIB_NAME_UPPER=$(echo "$LIB_NAME" | tr '[:lower:]' '[:upper:]' | tr '-' '_')

declare -a CREATED_FILES=()

emit_json() {
    local verdict="$1"
    local summary="$2"
    local build_status="${3:-N/A}"
    local build_output="${4:-}"
    local timestamp
    timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    local files_json=""
    for i in "${!CREATED_FILES[@]}"; do
        [[ $i -gt 0 ]] && files_json+=","$'\n'
        files_json+="    \"$(json_escape "${CREATED_FILES[$i]}")\""
    done

    cat <<EOF
{
  "script": "scaffold-lib",
  "timestamp": "$timestamp",
  "verdict": "$verdict",
  "lib_name": "$LIB_NAME",
  "kconfig_symbol": "CONFIG_$KCONFIG_SYMBOL",
  "sys_init_priority": $SYS_INIT_PRIORITY,
  "files_created": [
$files_json
  ],
  "build_status": "$build_status",
  "build_output": "$(json_escape "$build_output")",
  "summary": "$(json_escape "$summary")"
}
EOF
}

# ============================================================
# Create directory structure
# ============================================================
log "Scaffolding lib/$LIB_NAME..."

if [[ -z "$DRY_RUN" ]]; then
    mkdir -p "$REPO_ROOT/lib/$LIB_NAME/include/$LIB_NAME"
    mkdir -p "$REPO_ROOT/lib/$LIB_NAME/src"
fi

# ============================================================
# Kconfig
# ============================================================
KCONFIG_FILE="lib/$LIB_NAME/Kconfig"
CREATED_FILES+=("$KCONFIG_FILE")

if [[ -z "$DRY_RUN" ]]; then
    cat > "$REPO_ROOT/$KCONFIG_FILE" <<KCFG
# SPDX-License-Identifier: Apache-2.0

menuconfig $KCONFIG_SYMBOL
	bool "$DESCRIPTION"
	help
	  $DESCRIPTION

if $KCONFIG_SYMBOL

module = $KCONFIG_SYMBOL
module-str = $KCONFIG_SYMBOL
source "subsys/logging/Kconfig.template.log_config"

endif # $KCONFIG_SYMBOL
KCFG
    log "Created $KCONFIG_FILE"
else
    log "[DRY-RUN] Would create $KCONFIG_FILE"
fi

# ============================================================
# CMakeLists.txt
# ============================================================
CMAKE_FILE="lib/$LIB_NAME/CMakeLists.txt"
CREATED_FILES+=("$CMAKE_FILE")

if [[ -z "$DRY_RUN" ]]; then
    cat > "$REPO_ROOT/$CMAKE_FILE" <<CMAKE
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_$KCONFIG_SYMBOL)

  zephyr_library()
  zephyr_library_sources(src/${LIB_NAME}.c)
  zephyr_library_include_directories(include)
  zephyr_include_directories(include)

endif()
CMAKE
    log "Created $CMAKE_FILE"
else
    log "[DRY-RUN] Would create $CMAKE_FILE"
fi

# ============================================================
# Header
# ============================================================
HEADER_FILE="lib/$LIB_NAME/include/$LIB_NAME/${LIB_NAME}.h"
CREATED_FILES+=("$HEADER_FILE")

if [[ -z "$DRY_RUN" ]]; then
    cat > "$REPO_ROOT/$HEADER_FILE" <<HDR
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ${LIB_NAME_UPPER}_${LIB_NAME_UPPER}_H_
#define ${LIB_NAME_UPPER}_${LIB_NAME_UPPER}_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* zbus channel declarations (if this library owns a new channel) */
/* ZBUS_CHAN_DECLARE(${LIB_NAME}_chan); */

/* Public API */

#ifdef __cplusplus
}
#endif

#endif /* ${LIB_NAME_UPPER}_${LIB_NAME_UPPER}_H_ */
HDR
    log "Created $HEADER_FILE"
else
    log "[DRY-RUN] Would create $HEADER_FILE"
fi

# ============================================================
# Source
# ============================================================
SOURCE_FILE="lib/$LIB_NAME/src/${LIB_NAME}.c"
CREATED_FILES+=("$SOURCE_FILE")

if [[ -z "$DRY_RUN" ]]; then
    cat > "$REPO_ROOT/$SOURCE_FILE" <<SRC
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <sensor_event/sensor_event.h>
#include <${LIB_NAME}/${LIB_NAME}.h>

LOG_MODULE_REGISTER(${LIB_NAME}, CONFIG_${KCONFIG_SYMBOL}_LOG_LEVEL);

/* zbus listener callback */
static void ${LIB_NAME}_cb(const struct zbus_channel *chan)
{
	const struct env_sensor_data *evt = zbus_chan_const_msg(chan);
	ARG_UNUSED(evt);
}

ZBUS_LISTENER_DEFINE(${LIB_NAME}_listener, ${LIB_NAME}_cb);

static int ${LIB_NAME}_init(void)
{
	int rc = zbus_chan_add_obs(&sensor_event_chan, &${LIB_NAME}_listener, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("Failed to add observer: %d", rc);
		return rc;
	}
	LOG_INF("${LIB_NAME}: init done");
	return 0;
}

SYS_INIT(${LIB_NAME}_init, APPLICATION, ${SYS_INIT_PRIORITY});
SRC
    log "Created $SOURCE_FILE"
else
    log "[DRY-RUN] Would create $SOURCE_FILE"
fi

# ============================================================
# Register in lib/Kconfig
# ============================================================
LIB_KCONFIG="lib/Kconfig"
CREATED_FILES+=("$LIB_KCONFIG (modified)")

if [[ -z "$DRY_RUN" ]]; then
    if ! grep -q "rsource \"${LIB_NAME}/Kconfig\"" "$REPO_ROOT/$LIB_KCONFIG" 2>/dev/null; then
        echo "rsource \"${LIB_NAME}/Kconfig\"" >> "$REPO_ROOT/$LIB_KCONFIG"
        log "Registered in $LIB_KCONFIG"
    else
        log "Already registered in $LIB_KCONFIG"
    fi
else
    log "[DRY-RUN] Would register in $LIB_KCONFIG"
fi

# ============================================================
# Register in root CMakeLists.txt
# ============================================================
ROOT_CMAKE="CMakeLists.txt"
CREATED_FILES+=("$ROOT_CMAKE (modified)")

if [[ -z "$DRY_RUN" ]]; then
    if ! grep -q "add_subdirectory_ifdef(CONFIG_${KCONFIG_SYMBOL} lib/${LIB_NAME})" "$REPO_ROOT/$ROOT_CMAKE" 2>/dev/null; then
        echo "add_subdirectory_ifdef(CONFIG_${KCONFIG_SYMBOL} lib/${LIB_NAME})" >> "$REPO_ROOT/$ROOT_CMAKE"
        log "Registered in $ROOT_CMAKE"
    else
        log "Already registered in $ROOT_CMAKE"
    fi
else
    log "[DRY-RUN] Would register in $ROOT_CMAKE"
fi

# ============================================================
# Enable in gateway prj.conf
# ============================================================
GATEWAY_CONF="apps/gateway/prj.conf"
CREATED_FILES+=("$GATEWAY_CONF (modified)")

if [[ -z "$DRY_RUN" ]]; then
    if ! grep -q "CONFIG_${KCONFIG_SYMBOL}=y" "$REPO_ROOT/$GATEWAY_CONF" 2>/dev/null; then
        echo "CONFIG_${KCONFIG_SYMBOL}=y" >> "$REPO_ROOT/$GATEWAY_CONF"
        log "Enabled in $GATEWAY_CONF"
    else
        log "Already enabled in $GATEWAY_CONF"
    fi
else
    log "[DRY-RUN] Would enable in $GATEWAY_CONF"
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
    emit_json "DRY_RUN" "Would create ${#CREATED_FILES[@]} files." "$BUILD_STATUS" "$BUILD_OUTPUT"
else
    emit_json "PASS" "Created ${#CREATED_FILES[@]} files for lib/$LIB_NAME." "$BUILD_STATUS" "$BUILD_OUTPUT"
fi

exit 0
